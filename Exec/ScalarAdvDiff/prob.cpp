#include "prob.H"

ProbParm parms;

void
erf_init_dens_hse(amrex::Real* dens_hse_ptr,
                  amrex::GeometryData const& geomdata,
                  const int ng_dens_hse)
{
  const int khi = geomdata.Domain().bigEnd()[2];
  for (int k = -ng_dens_hse; k <= khi+ng_dens_hse; k++)
  {
      dens_hse_ptr[k] = parms.rho_0;
  }
}

void
erf_init_rayleigh(amrex::Vector<amrex::Real>& tau,
                  amrex::Vector<amrex::Real>& ubar,
                  amrex::Vector<amrex::Real>& vbar,
                  amrex::Vector<amrex::Real>& thetabar,
                  amrex::GeometryData  const& geomdata)
{
  const int khi              = geomdata.Domain().bigEnd()[2];

  // We just use these values to test the Rayleigh damping
  for (int k = 0; k <= khi; k++)
  {
      tau[k]  = 1.0;
      ubar[k] = 2.0;
      vbar[k] = 1.0;
      thetabar[k] = parms.Theta_0;
  }
}

void
erf_init_prob(
  const amrex::Box& bx,
  amrex::Array4<amrex::Real> const& state,
  amrex::Array4<amrex::Real> const& x_vel,
  amrex::Array4<amrex::Real> const& y_vel,
  amrex::Array4<amrex::Real> const& z_vel,
  amrex::GeometryData const& geomdata)
{
  amrex::ParallelFor(bx, [=, parms=parms] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
    // Geometry
    const amrex::Real* prob_lo = geomdata.ProbLo();
    const amrex::Real* prob_hi = geomdata.ProbHi();
    const amrex::Real* dx = geomdata.CellSize();
    const amrex::Real x = prob_lo[0] + (i + 0.5) * dx[0];
    const amrex::Real y = prob_lo[1] + (j + 0.5) * dx[1];
    const amrex::Real z = prob_lo[2] + (k + 0.5) * dx[2];

    // Define a point (xc,yc,zc) at the center of the domain
    const amrex::Real xc = parms.xc_frac * (prob_lo[0] + prob_hi[0]);
    const amrex::Real yc = parms.yc_frac * (prob_lo[1] + prob_hi[1]);
    const amrex::Real zc = parms.zc_frac * (prob_lo[2] + prob_hi[2]);

    const amrex::Real r3d  = std::sqrt((x-xc)*(x-xc) + (y-yc)*(y-yc) + (z-zc)*(z-zc));
    const amrex::Real r2d  = std::sqrt((x-xc)*(x-xc) + (y-yc)*(y-yc));

    const amrex::Real r0 = parms.rad_0 * (prob_hi[0] - prob_lo[0]);

    // Set the density
    state(i, j, k, Rho_comp) = parms.rho_0;

    // Initial potential temperature
    state(i, j, k, RhoTheta_comp) = parms.rho_0 * parms.Theta_0;

    if (parms.prob_type == 10)
    {
        state(i, j, k, RhoScalar_comp) = parms.A_0 * exp(-10.*r3d*r3d) + parms.B_0*sin(x);
        // Set scalar = A_0*exp(-10r^2), where r is distance from center of domain,
        //            + B_0*sin(x)
        state(i, j, k, RhoScalar_comp) = parms.A_0 * exp(-10.*r3d*r3d) + parms.B_0 * sin(x);

    } else if (parms.prob_type == 11) {
        state(i, j, k, RhoScalar_comp) = parms.A_0 * 0.25 * (1.0 + std::cos(PI * std::min(r2d, r0) / r0));
    } else {
        // Set scalar = A_0 in a ball of radius r0 and 0 elsewhere
        if (r3d < r0) {
           state(i, j, k, RhoScalar_comp) = parms.A_0;
        } else {
           state(i, j, k, RhoScalar_comp) = 0.0;
        }
    }

  });

  // Construct a box that is on x-faces
  const amrex::Box& xbx = amrex::surroundingNodes(bx,0);
  // Set the x-velocity
  amrex::ParallelFor(xbx, [=, parms=parms] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
    x_vel(i, j, k) = parms.u_0;

    const amrex::Real* prob_lo = geomdata.ProbLo();
    const amrex::Real*      dx = geomdata.CellSize();
    const amrex::Real        y = prob_lo[1] + (j + 0.5) * dx[1];
    const amrex::Real        z = prob_lo[2] + (k + 0.5) * dx[2];

    // Set the x-velocity
    if (parms.prob_type == 11) {
        x_vel(i, j, k) = -y + 0.5;
    } else {
        x_vel(i, j, k) = parms.u_0 + parms.uRef *
                         std::log((z + parms.z0)/parms.z0)/
                         std::log((parms.zRef +parms.z0)/parms.z0);
    }
  });

  // Construct a box that is on y-faces
  const amrex::Box& ybx = amrex::surroundingNodes(bx,1);
  // Set the y-velocity
  amrex::ParallelFor(ybx, [=, parms=parms] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {

    const amrex::Real* prob_lo = geomdata.ProbLo();
    const amrex::Real*      dx = geomdata.CellSize();
    const amrex::Real        x = prob_lo[0] + (i + 0.5) * dx[0];

    if (parms.prob_type == 11) {
        y_vel(i, j, k) = x - 0.5;
    } else {
        y_vel(i, j, k) = parms.v_0;
    }
  });

  // Construct a box that is on z-faces
  const amrex::Box& zbx = amrex::surroundingNodes(bx,2);
  // Set the z-velocity
  amrex::ParallelFor(zbx, [=, parms=parms] AMREX_GPU_DEVICE(int i, int j, int k) noexcept {
    z_vel(i, j, k) = 0.0;
  });
}

void
erf_prob_close()
{
}

extern "C" {
void
amrex_probinit(
  const int* /*init*/,
  const int* /*name*/,
  const int* /*namelen*/,
  const amrex_real* /*problo*/,
  const amrex_real* /*probhi*/)
  {
    // Parse params
    amrex::ParmParse pp("prob");
    pp.query("rho_0", parms.rho_0);
    pp.query("T_0", parms.Theta_0);
    pp.query("A_0", parms.A_0);
    pp.query("B_0", parms.B_0);
    pp.query("u_0", parms.u_0);
    pp.query("v_0", parms.v_0);
    pp.query("rad_0", parms.rad_0);
    pp.query("z0", parms.z0);
    pp.query("zRef", parms.zRef);
    pp.query("uRef", parms.uRef);

    pp.query("xc_frac", parms.xc_frac);
    pp.query("yc_frac", parms.yc_frac);
    pp.query("zc_frac", parms.zc_frac);

    pp.query("prob_type", parms.prob_type);
  }
}