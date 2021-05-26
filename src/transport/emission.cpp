#include <math.h>
#include <gsl/gsl_rng.h>
#include "transport.h"
#include "radioactive.h"
#include "physical_constants.h"

namespace pc = physical_constants;
using std::cout;
using std::cerr;
using std::endl;

//------------------------------------------------------------
// emit new particles
//------------------------------------------------------------
void transport::emit_particles(double dt)
{
  emit_radioactive(dt);
  emit_thermal(dt);
  //emit_heating_source(dt);
  emit_inner_source(dt);
  emit_from_pointsoures(dt);
}

//------------------------------------------------------------
// sample photon frequency from local emissivity
//------------------------------------------------------------
void transport::sample_photon_frequency(particle *p)
{
  if (p->type == photon)
  {
    int inu  = emissivity_[p->ind].sample(rangen.uniform());
    p->nu = nu_grid_.sample(inu,rangen.uniform());
    if (p->nu > 1e20) std::cout << "pnu " << p->nu << "\n";
  }
  else if (p->type == gammaray)
  {
    p->nu = 1;
  }
  else
  {
    p->nu = 1;
  }



}

//------------------------------------------------------------
// General function to create a particle in zone i
// emitted isotropically in the comoving frame.
// Useful for thermal radiation emitted all througout
// the grid
//------------------------------------------------------------
void transport::create_isotropic_particle
(int i, PType type, double Ep, double t)
{
  particle p;

  // particle index
  p.ind = i;

  // particle type
  p.type = type;

  // random sample position in zone
  std::vector<double> rand;
  rand.push_back(rangen.uniform());
  rand.push_back(rangen.uniform());
  rand.push_back(rangen.uniform());
  double r[3];
  grid->sample_in_zone(i,rand,r);
  p.x[0] = r[0];
  p.x[1] = r[1];
  p.x[2] = r[2];

  p.x_interact[0] = r[0];
  p.x_interact[1] = r[1];
  p.x_interact[2] = r[2];

  // emit isotropically in comoving frame
  double mu  = 1 - 2.0*rangen.uniform();
  double phi = 2.0*pc::pi*rangen.uniform();
  double smu = sqrt(1 - mu*mu);
  p.D[0] = smu*cos(phi);
  p.D[1] = smu*sin(phi);
  p.D[2] = mu;

  // sample frequency from local emissivity
  sample_photon_frequency(&p);
//  p.nu = 1e16; //debug

  // set packet energy
  p.e  = Ep;

  // lorentz transform from the comoving to lab frame
  transform_comoving_to_lab(&p);

  // set time to current
  p.t  = t;

  // add to particle vector
  #pragma omp critical
  particles.push_back(p);

}


//------------------------------------------------------------
// Initialize a constant number of particles
// in each zone
//------------------------------------------------------------
void transport::initialize_particles(int init_particles)
{
  int my_n_emit = init_particles/(1.0*MPI_nprocs);
  // If init_particles % MPI_nprocs != 0, create the remaining particles
  // on the first remainder nodes.
  int remainder = init_particles % MPI_nprocs;
  if (MPI_myID < remainder) {
    my_n_emit += 1;
  }

  if (my_n_emit == 0) return;
   // check that we have enough space to add these particles
  if (my_n_emit > max_total_particles) {
      if (verbose) cerr << "# Not enough particle space to initialize" << endl;
      return; }

  if (verbose) cout << "# init with " << init_particles << " total particles ";
  if (verbose) cout << "(" << my_n_emit << " per MPI proc)\n";
  if (my_n_emit == 0) return;
  
  // for monochromatic emission
  double nu_emit = params_->getScalar<double>("particles_init_photon_frequency");
  bool blackbody = (nu_emit == 0);

  // set up emission distribution across zones
  double E_sum = 0;
  int ng = nu_grid_.size();
  for (int i=0;i<grid->n_zones;i++)
  {
    double T = grid->z[i].T_gas;
    double E_zone = grid->z[i].e_rad*grid->zone_volume(i);
    zone_emission_cdf_.set_value(i,E_zone);
    E_sum += E_zone;
    
    if(blackbody)
    {
      // setup blackbody emissivity for initialization
      for (int j=0;j<ng;j++)
      {
        double nu_m = nu_grid_.center(j);
        double emis = blackbody_nu(T,nu_m)*nu_grid_.delta(j);
        emissivity_[i].set_value(j,emis);
      }
    }
    else
    {
      // do monochromatic emission
      for (int j=0;j<ng;j++)
      {
        if(nu_grid_.left(j) <= nu_emit && nu_grid_.right(j) >= nu_emit)
        {
          emissivity_[i].set_value(j,1);
        }
        else
        {
          emissivity_[i].set_value(j,0);
        }
      }
    }
    
    emissivity_[i].normalize();
  }
  zone_emission_cdf_.normalize();

  // emit particles
  double Ep = E_sum/(1.0*my_n_emit);
  for (int q=0;q<my_n_emit;q++)
  {
    int i = zone_emission_cdf_.sample(rangen.uniform());
    create_isotropic_particle(i,photon,Ep,t_now_);
  }
}


//------------------------------------------------------------
// Emit gamma-rays from radioactive decay
//------------------------------------------------------------
void transport::emit_radioactive(double dt)
{
  // number of radioctive particles to emit
  int total_n_emit = params_->getScalar<int>("particles_n_emit_radioactive");

  if (params_->getScalar<int>("multiply_particles_n_emit_by_dt_over_dtmax"))
  {
    total_n_emit *= dt / params_->getScalar<double>("tstep_max_dt");
  }

  if (total_n_emit == 0) return;

  // whether or not to use r-process heating, regardless of composition
  int force_rproc = params_->getScalar<int>("force_rprocess_heating");

  if (last_iteration_)
  {
    int pumpup = params_->getScalar<int>("particles_last_iter_pump");
    if (pumpup != 0) total_n_emit *= pumpup;
    if (verbose) std::cout << "# last iteration, increasing emission by factor of " << pumpup << "\n";
  }

  int my_n_emit = floor(total_n_emit/(1.0*MPI_nprocs));

  // randomize remainder
  double remainder = total_n_emit/(1.0*MPI_nprocs) - my_n_emit;
  if (rangen.uniform() < remainder) my_n_emit += 1;

  radioactive radio;
  double gfrac;

  // calculate the total decay energy on the grid
  double L_tot = 0;
  double *gamma_frac = new double[grid->n_zones];
  for (int i=0;i<grid->n_zones;i++)
  {
    double vol  = grid->zone_volume(i);
    double L_decay =
      radio.decay(grid->elems_Z,grid->elems_A,grid->z[i].X_gas,t_now_,&gfrac, force_rproc);
    L_decay = grid->z[i].rho*L_decay*vol;
    grid->z[i].L_radio_emit = L_decay;
    gamma_frac[i] = gfrac;
    L_tot += L_decay;
    zone_emission_cdf_.set_value(i,L_decay);
  }
  zone_emission_cdf_.normalize();


  if (L_tot == 0) return;
  double E_p = L_tot*dt/(1.0*my_n_emit);

  // check that we have enough space to add these particles
  if ((int)particles.size()+my_n_emit > max_total_particles) {
    if (verbose) cerr << "# Out of particle space; not adding in" << endl;
    return; }

  // emit particles
  for (int q=0;q<my_n_emit;q++)
  {
    int i = zone_emission_cdf_.sample(rangen.uniform());
    double t  = t_now_ + dt*rangen.uniform();

    // determine if make gamma-ray or positron
    if (rangen.uniform() < gamma_frac[i])
      create_isotropic_particle(i,gammaray,E_p,t);
    else
    {
      // positrons are just immediately made into photons
      #pragma omp atomic
      grid->z[i].L_radio_dep += E_p;
      create_isotropic_particle(i,photon,E_p,t);
    }
  }

  if (verbose) cout << "# L_radioactive = " << L_tot << " ergs/s; ";
  if (verbose) cout << "added " << total_n_emit << " particles ";
  if (verbose) cout << "(" << my_n_emit << " per MPI proc)\n";
  delete[] gamma_frac;
}



void transport::emit_thermal(double dt)
{
  // number of thermal particles to emit
  int total_n_emit = params_->getScalar<int>("particles_n_emit_thermal");
  if (total_n_emit == 0) return;
  int my_n_emit = total_n_emit/(1.0*MPI_nprocs);

  // calculate the total thermal emisison energy on the grid
  double E_tot = 0;
  for (int i=0;i<grid->n_zones;i++)
  {
    double vol  = grid->zone_volume(i);
    //t_emit defined below actually has units of 1/time. This is for comoving frame
   // double t_emit = planck_mean_opac*pc::c;
      //comoving frame emission energy. Note that dt * vol is frame invariant
    double E_zone_emit = grid->z[i].L_thermal*vol*dt * grid->z[i].eps_imc; //pc::a*pow(T_gas,4)*t_emit*dt*vol*grid->z[i].eps_imc;
    // save the comoving frame thermal emission
    // you divide by lab frame volume because this is also divided by lab frame time,
    // and together the vol * dt is frame invariant.
    //grid->z[i].e_emit = E_emit/vol;
    //This needs to come after the line where you set e_emit = E_emit/vol ;
    // you've already accounted for the corresponding effect on the hydro in hydro.cc
    //E_emit += grid->z[i].Sgam;
    E_tot += E_zone_emit;
    zone_emission_cdf_.set_value(i,E_zone_emit);
  }
  zone_emission_cdf_.normalize();

  if (E_tot == 0) return;
  double E_p = E_tot/(1.0*my_n_emit);

  // emit particles
  for (int q=0;q<my_n_emit;q++)
  {
    int i = zone_emission_cdf_.sample(rangen.uniform());
    double t  = t_now_ + dt*rangen.uniform();
    create_isotropic_particle(i,photon,E_p,t);
  }

  if (verbose) cout << "# E thermal = " << E_tot << " ergs; ";
  if (verbose) cout << "added " << total_n_emit << " particles ";
  if (verbose) cout << "(" << my_n_emit << " per MPI proc)\n";
}

    // For efficient energy redistribtuion, assign each emitted particle a fraction of the zone's current radiation energy.
    // There is a default value for 10000 emitted particles per zone, but emit_max and emit_min will usually determine
    // the number of particles emitted, and hence, the energy per particle.
    //double E_zone  = grid->z[i].e_rad*vol;

    //double Ep      = E_zone * 0.0001;
    //long int n_add = (int)(E_emit/Ep);
    //if (Ep == 0) n_add = 0;
    //if (n_add < 0) n_add = 0;

  //  Ep = E_emit/n_add;

    // minimum add
    //if ((E_emit > 0)&&(n_add < emit_min)) {
     // n_add = emit_min;
     // Ep = E_emit/n_add; }

    // maximum add
    //if (n_add > emit_max) {
    //  n_add = emit_max;
     // Ep = E_emit/n_add; }

    // but don't add if nothing here
    //if (E_emit == 0) n_add = 0;

    //double E_min  = RAD_CONST*pow(1.0,4.0)*vol;
    //if (E_zone < E_min) E_zone = E_min;
    //double ez_gas = grid->z[i].e_gas*grid->z[i].rho*vol;
    //if (E_zone < ez_gas) E_zone = ez_gas;
    //E_zone = ez_gas;
    // if (n_add < 0) printf("nadd %d %e %e %e %e\n"
    //,n_add,E_emit,Ep,t_emit,grid->z[i].rho);

    //if (n_particles+n_add > max_particles)
     // Rebuffer_Particles();

    //if (n_particles+n_add > max_particles) {
    //  printf("Ran out of particle space\n");
    //  return; }

    // setup particles
    //for (int q=n_particles;q<n_particles+n_add;q++)
    //  {
  //reate_Isotropic_Particle(&(particle[q]),i); // Isotropic in the comoving frame; not necessarily isotropic in the lab frame

  //particle[q].energy *= Ep; // you already have a doppler shift correction stored here

  // momentum exchange from emission
  // later in transport.cc this gets divided by vol * C_LIGHT

  //  grid->z[i].fx_rad -= particle[q].energy * particle[q].D[0];
  //  grid->z[i].fy_rad -= particle[q].energy * particle[q].D[1];
  //  grid->z[i].fz_rad -= particle[q].energy * particle[q].D[2];

  // e_emit might already be recorded based on the temperature. or, you could do it photon by photon here
  //grid->z[i].e_emit += particle[q].energy/vol;

  //particle[q].t      = t_now + dt*rangen.uniform();
  //    }
  //    n_particles += n_add;
  //    n_add_tot   += n_add;
  //  }
  //  if (verbose) printf("Added %ld particles\n",n_add_tot);

//------------------------------------------------------------
// A generic heating source, hacked up for now
//------------------------------------------------------------
void transport::emit_heating_source(double dt)
{
  double Ep = 1e52;
  double tp = 3600.0*24.0*20.0;
  double Lheat = Ep/tp/(1 + t_now_/tp)/(1 + t_now_/tp);
  L_core_ = Lheat;
}


//------------------------------------------------------------
// inject particles from a central luminous source
//------------------------------------------------------------
void transport::emit_inner_source(double dt)
{
  // get the emisison properties from lua file
  // this could be set to be a function if we want
  int total_n_emit    = params_->getScalar<int>("core_n_emit");
  if (total_n_emit == 0) return;

  if (last_iteration_)
  {
    int pumpup = params_->getScalar<int>("particles_last_iter_pump");
    if (pumpup != 0) total_n_emit *= pumpup;
    if (verbose) std::cout << "# last iteration, increasing emission by factor of " << pumpup << "\n";
  }
  int n_emit = total_n_emit/(1.0*MPI_nprocs);


  // get current luminosity, if time dependent
  double L_current = params_->getFunction("core_luminosity", t_now_);
  if (L_current != 0) L_core_ = L_current;
  double Ep  = L_core_*dt/n_emit;

  if ((int)particles.size() + n_emit > this->max_total_particles)
    {cerr  << "# Not enough particle space" << endl; return; }

  // inject particles from the source
  for (int i=0;i<n_emit;i++)
  {
    particle p;

    if (r_core_ == 0)
    {
      // central emission
      p.x[0] = 0;
      p.x[1] = 0;
      p.x[2] = 0;
      // emit isotropically in comoving frame
      double mu  = 1 - 2.0*rangen.uniform();
      double phi = 2.0*pc::pi*rangen.uniform();
      double smu = sqrt(1 - mu*mu);
      p.D[0] = smu*cos(phi);
      p.D[1] = smu*sin(phi);
      p.D[2] = mu;
    }
    else
    {
      // pick initial position on photosphere
      double phi_core   = 2*pc::pi*rangen.uniform();
      double cosp_core  = cos(phi_core);
      double sinp_core  = sin(phi_core);
      double cost_core  = 1 - 2.0*rangen.uniform();
      double sint_core  = sqrt(1-cost_core*cost_core);
      // real spatial coordinates
      double a_phot = r_core_ + r_core_*1e-10;
      p.x[0] = a_phot*sint_core*cosp_core;
      p.x[1] = a_phot*sint_core*sinp_core;
      p.x[2] = a_phot*cost_core;

      // pick photon propagation direction wtr to local normal
      double phi_loc = 2*pc::pi*rangen.uniform();
      // choose sqrt(R) to get outward, cos(theta) emission
      double cost_loc  = sqrt(rangen.uniform());
      double sint_loc  = sqrt(1 - cost_loc*cost_loc);
      // local direction vector
      double D_xl = sint_loc*cos(phi_loc);
      double D_yl = sint_loc*sin(phi_loc);
      double D_zl = cost_loc;
      // apply rotation matrix to convert D vector into overall frame
      p.D[0] = cost_core*cosp_core*D_xl-sinp_core*D_yl+sint_core*cosp_core*D_zl;
      p.D[1] = cost_core*sinp_core*D_xl+cosp_core*D_yl+sint_core*sinp_core*D_zl;
      p.D[2] = -sint_core*D_xl+cost_core*D_zl;
    }

    p.x_interact[0] = p.x[0];
    p.x_interact[1] = p.x[1];
    p.x_interact[2] = p.x[2];

    // set energy of packet
    p.e = Ep;

    // get emission frequency
    if (core_frequency_ > 0)
    {
      // constant single frequency emission
      p.nu = core_frequency_;
    }
    else
    {
      // sample frequency from blackbody
      int inu = core_emission_spectrum_.sample(rangen.uniform());
      p.nu = nu_grid_.sample(inu,rangen.uniform());
      p.e  /= emissivity_weight_[inu];
      // straight bin emission
      //int ilam = rangen.uniform()*nu_grid_.size();
      //p.e *= core_emis.get_value(ilam)*nu_grid_.size();
    }

    // get index of current zone
    p.ind = grid->get_zone(p.x);

    // lorentz transform from the comoving to lab frame
    transform_comoving_to_lab(&p);

    // set time to current
    p.t  = t_now_ + rangen.uniform()*dt;

    // set type to photon
    p.type = photon;

    // add to particle vector
    #pragma omp critical
    particles.push_back(p);
  }

  if (verbose)
    printf("# L_core = %e; emitted %d particles (%d per proc)\n",L_core_,total_n_emit,n_emit);
}


//------------------------------------------------------------
// inject particles from point source
//------------------------------------------------------------
void transport::emit_from_pointsoures(double dt)
{
  if (!use_pointsources_) return;

  // get the emisison properties from lua file
  // this could be set to be a function if we want
  int total_n_emit    = params_->getScalar<int>("particles_n_emit_pointsources");
  if (total_n_emit == 0) return;
  int n_emit = total_n_emit/(1.0*MPI_nprocs);

  if ((int)particles.size() + n_emit > this->max_total_particles)
    {cerr << "# Not enough particle space" << endl; return; }

  double Ep  = pointsources_L_tot_*dt/n_emit;

  // inject particles from the source
  for (int i=0;i<n_emit;i++)
  {
    particle p;

    // pick your pointsource to emit from
    int ind = pointsource_emission_cdf_.sample(rangen.uniform());

    p.x[0] = pointsource_x_[ind];
    p.x[1] = pointsource_y_[ind];
    p.x[2] = pointsource_z_[ind];
    
    p.x_interact[0] = p.x[0];
    p.x_interact[1] = p.x[1];
    p.x_interact[2] = p.x[2];

    // emit isotropically in comoving frame
    double mu  = 1 - 2.0*rangen.uniform();
    double phi = 2.0*pc::pi*rangen.uniform();
    double smu = sqrt(1 - mu*mu);
    p.D[0] = smu*cos(phi);
    p.D[1] = smu*sin(phi);
    p.D[2] = mu;

    // set energy of packet
    p.e = Ep;

    // sample frequency
    int inu = pointsource_emission_spectrum_.sample(rangen.uniform());
    p.nu = nu_grid_.sample(inu,rangen.uniform());

    // get index of current zone
    p.ind = grid->get_zone(p.x);

    // lorentz transform from the comoving to lab frame
    transform_comoving_to_lab(&p);

    // set time to current
    p.t  = t_now_ + rangen.uniform()*dt;

    // set type to photon
    p.type = photon;

    // add to particle vector
    #pragma omp critical
    particles.push_back(p);
  }

  if (verbose)
    printf("# pointsource = %e; emitted %d particles (%d per proc)\n",pointsources_L_tot_,total_n_emit,n_emit);
}
