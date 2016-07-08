#include <mpi.h>
#include <math.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <limits>
#include <vector>
#include <cassert>
#include <list>
#include <algorithm>

#include "transport.h"
#include "ParameterReader.h"
#include "physical_constants.h"

using std::cout;
namespace pc = physical_constants;

//------------------------------------------------------------
// take a transport time step 
//------------------------------------------------------------
void transport::step(double dt)
{
  // nominal time for iterative calc is 1
  if (this->steady_state) dt = 1;
  
  // calculate opacities
  set_opacity();
  if (verbose) cout << "# Calculated opacities\n";

  // clear the tallies of the radiation quantities in each zone
  wipe_radiation();

  // emit new particles
  emit_particles(dt);
  
  // Propagate the particles
  int n_active = particles.size();
  int n_escape = 0;
  std::list<particle>::iterator pIter = particles.begin();
  while (pIter != particles.end())
  {
    ParticleFate fate = propagate(*pIter,dt);
    if (fate == escaped) n_escape++;
    if ((fate == escaped)||(fate == absorbed)) pIter = particles.erase(pIter);
    else pIter++;
  }

  // calculate percent particles escaped, and rescale if wanted
  double per_esc = (1.0*n_escape)/(1.0*n_active);
  if ((verbose)&&(steady_state)) {
    cout << "# Percent particles escaped = " << 100.0*per_esc << "\n";
    optical_spectrum.rescale(1.0/per_esc); }

  // normalize and MPI combine radiation tallies
  reduce_radiation(dt);

  // solve for T_gas structure if radiative eq. applied
  if (radiative_eq) solve_eq_temperature();
   
  // advance time step
  if (!steady_state) t_now_ += dt;
}



//--------------------------------------------------------
// Propagate a single monte carlo particle until
// it  escapes, is absorbed, or the time step ends
//--------------------------------------------------------
ParticleFate transport::propagate(particle &p, double dt)
{
  enum ParticleEvent {scatter, boundary, tstep};
  ParticleEvent event;
  
  // To be sure, get initial position of the particle 
  ParticleFate  fate = moving;
  p.ind = grid->get_zone(p.x);
  
  if (p.ind == -1) {return absorbed;}
  if (p.ind == -2) {return  escaped;}
  
  // time of end of timestep
  double tstop = t_now_ + dt;

  // pointer to current zone
  zone *zone = &(grid->z[p.ind]);


  // propagate until this flag is set
  while (fate == moving)
  {
    // set pointer to current zone
    zone = &(grid->z[p.ind]);
    
    double d_bn; 
    int new_ind = grid->get_next_zone(p.x,p.D,p.ind,r_core_,&d_bn);

    // maximum step size inside zone
    //double d_bn = step_size_*grid->zone_min_length(p.ind); //*gsl_rng_uniform(rangen);

    // determine the doppler shift from comoving to lab
    //double dshift = dshift_comoving_to_lab(&p);
    double dshift = dshift_lab_to_comoving(&p);

    // get continuum opacity and absorption fraction (epsilon)
    double continuum_opac_cmf, eps_absorb_cmf;
    int i_nu = get_opacity(p,dshift,continuum_opac_cmf,eps_absorb_cmf);

    // convert opacity from comoving to lab frame for the purposes of 
    // determining the interaction distance in the lab frame
    // This corresponds to equation 90.8 in Mihalas&Mihalas. You multiply 
    // the comoving opacity by nu_0 over nu, which is why you
    // multiply by dshift instead of dividing by dshift here
    double tot_opac_cmf      = continuum_opac_cmf;
    double tot_opac_labframe = tot_opac_cmf*dshift;
    
    // random optical depth to next interaction
    double tau_r = -1.0*log(1 - gsl_rng_uniform(rangen));
    
    // step size to next interaction event
    double d_sc  = tau_r/tot_opac_labframe;
    if (tot_opac_labframe == 0) d_sc = std::numeric_limits<double>::infinity();
    if (d_sc <= 0) 
      cout << "ERROR: non-positive interaction distance! " << p.nu << " " << dshift << " " <<
        tot_opac_labframe  << "\n";
    //std::cout << d_sc << "\t" << d_bn << "\t" << tot_opac_labframe << "\n";


    // find distance to end of time step
    double d_tm = (tstop - p.t)*pc::c;
    // if iterative calculation, give infinite time for particle escape
    if (this->steady_state) d_tm = std::numeric_limits<double>::infinity();

    //std::cout << d_bn << "\t" << d_sc << "\n";

    // find out what event happens (shortest distance)
    double this_d;
    if ((d_sc < d_bn)&&(d_sc < d_tm))
      {event = scatter;    this_d = d_sc;}
    else if (d_bn < d_tm)
      {event = boundary;   this_d = d_bn;}
    else 
      {event = tstep;      this_d = d_tm; }

    // tally in contribution to zone's radiation energy (both *lab* frame)
    double this_E = p.e*this_d; 
    zone->e_rad  += this_E; 

    // store absorbed energy in *comoving* frame 
    // (will turn into rate by dividing by dt later)
    // Extra dshift definitely needed here (two total)
    // don't add gamma-rays here (they would be separate)
    if (p.type == photon)
    {
      zone->e_abs  += this_E*dshift*(continuum_opac_cmf)*eps_absorb_cmf*dshift;
      J_nu_[p.ind][i_nu] += this_E;
    }
      
    // put back in radiation force tally here
    // fx_rad =

    // move particle the distance
    p.x[0] += this_d*p.D[0];
    p.x[1] += this_d*p.D[1];
    p.x[2] += this_d*p.D[2]; 
    // advance the time
    p.t = p.t + this_d/pc::c;

    // get photon radius
    //double r_p = p.r();

    // check for reflection back
    /*double r_out = 1.0098e14;
    if (r_p > r_out)
    {
      // flip direction
       p.D[0] *= -1;
       p.D[1] *= -1;
       p.D[2] *= -1;
       // put on edge, slightly in
       p.x[0] *= (1. - 1.e-16) * r_out/r_p;
       p.x[1] *= (1. - 1.e-16) * r_out/r_p;
       p.x[2] *= (1. - 1.e-16) * r_out/r_p;
    } */

    // check for inner boundary absorption
    //if (r_p < r_core_)  {fate = absorbed;}

    // Find position of the particle now
    //p.ind = grid->get_zone(p.x);
    //i
    //if (p.ind == -2) fate = escaped;

    // ---------------------------------
    // do a boundary event
    // ---------------------------------
    if (event == boundary)
    {
      p.ind = new_ind;
      if (p.ind == -1) fate = absorbed;
      if (p.ind == -2) fate = escaped;
    }

    // ---------------------------------
    // do an interaction event
    // ---------------------------------
    else if (event == scatter)  
    {   
       if (gsl_rng_uniform(rangen) > eps_absorb_cmf) 
        fate = do_scatter(&p,eps_absorb_cmf);
        else
        fate = absorbed;
        //if (gsl_rng_uniform(rangen) > 0.38)
       // fate = absorbed; 
       //else fate = do_scatter(&p,0); 
         // debug
//        fate = do_scatter(&p,eps_absorb_cmf);  

     }

    // ---------------------------------
    // do an end of timestep event
    // ---------------------------------
    else if (event == tstep) 
       fate = stopped;
  }

  // Add escaped photons to output spectrum
  if (fate == escaped) 
  {
    // account for light crossing time, relative to grid center
    double xdot = p.x[0]*p.D[0] + p.x[1]*p.D[1] + p.x[2]*p.D[2];
    double t_obs = p.t - xdot/pc::c;
    if (p.type == photon)   optical_spectrum.count(t_obs,p.nu,p.e,p.D);
    if (p.type == gammaray) gamma_spectrum.count(t_obs,p.nu,p.e,p.D);
  }
  return fate;
}



