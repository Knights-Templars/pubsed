#ifndef _TRANSPORT_H
#define _TRANSPORT_H

#include <vector>
#include <list>
#include <mpi.h>

#include "particle.h"
#include "grid_general.h"
#include "cdf_array.h"
#include "locate_array.h"
#include "thread_RNG.h"
#include "spectrum_array.h"
#include "nlte_gas.h"
#include "ParameterReader.h"

using std::vector;


/* class RelativityData */
/* { */
/*   double gamma; */
/*   double beta; */
/*   double V[3]; */
/*   double D[3]; */
/*   double v_dot_D; */
/* }; */
  

class transport
{

 private:

  // arrays of particles
  std::list<particle> particles;
  int max_total_particles;

  // gas class for opacities
  nlte_gas gas;

  // pointer to parameter reader
  ParameterReader* params_;
  
  // MPI stuff
  int MPI_nprocs;
  int MPI_myID;
  MPI_Datatype MPI_real;
  
  // simulation parameters
  double step_size_;
  int    steady_state;
  int    radiative_eq;
  int    verbose;

  // current time in simulation
  double t_now_;
  // inner boundary
  double r_core_;

  // class to hold output spectrum
  spectrum_array optical_spectrum;
  spectrum_array gamma_spectrum;

  // random number generator
  //mutable thread_RNG rangen;

   // random number generator
  gsl_rng *rangen;

  // pointer to grid
  grid_general *grid;
  
  // the frequency grid for emissivity/opacity (Hz)
  locate_array nu_grid;

  // the core emissivity (erg/s - units of N)
  cdf_array core_emis;

  // the zone opacity/emissivity variables
  vector< cdf_array >    emissivity_;
  vector< vector<real> > abs_opacity_;
  vector< vector<real> > scat_opacity_;
  vector<real> compton_opac;
  vector<real> photoion_opac;

  // the radiation quantities in the zone
  vector <real> e_rad;
  
  // line data, has dimensions n_lines
  vector <real> line_nu_;
  vector <int>  line_lowerLevel_;
  vector <int>  line_upperLevel_;
  vector <real> line_f_osc_;
  vector <real> line_Jbar_;

  // line mean intensity
  //vector< vector<real> > J_line;
  // level populations, has dimensions n_levels
  //vector< vector<real> > level_population;


  // opacity functions
  void   get_opacity(particle&, double, double&, double&);
  void   set_opacity();
  double klein_nishina(double);
  double blackbody_nu(double T, double nu);

  // creation of particles functions
  void   emit_particles(double dt);
  void   emit_inner_source(double dt);
  void   emit_radioactive(double dt);
  void   create_isotropic_particle(int,PType,double,double);
  void   initialize_particles(int);
  void sample_photon_frequency(particle*);
  
  // special relativistic functions
  void   transform_comoving_to_lab(particle*) const;
  void   transform_lab_to_comoving(particle*) const;
  double dshift_comoving_to_lab(particle*) const;
  double dshift_lab_to_comoving(particle*) const;

  //propagation of particles functions
  ParticleFate propagate(particle &p, double tstop);

  // scattering functions
  ParticleFate do_scatter(particle*, double);
  void compton_scatter(particle*);
  void isotropic_scatter(particle*, int);

  // radiation quantities functions
  void wipe_radiation();
  void reduce_radiation(double);

  // solve equilibrium temperature
  void solve_eq_temperature();
  double rad_eq_function(int,double);
  double temp_brent_method(int);



 public:
  

  //--------------------------------
  // constructor and defaults
  //------------------------------

  transport()
  {
  }

  //----- functions ---------------
  
  // set things up
  void init(ParameterReader*, grid_general*);
  
  // run a transport step
  void step(double dt);

  // return 
  int n_particles() { return particles.size(); }

  // finalize and output spectra
  void output_spectrum();
  void output_spectrum(int);
					

};


#endif
