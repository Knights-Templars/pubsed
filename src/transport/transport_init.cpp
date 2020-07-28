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
#include <iomanip>

#include "transport.h"
#include "ParameterReader.h"
#include "physical_constants.h"


using std::cout;
using std::cerr;
using std::endl;
namespace pc = physical_constants;

namespace
{
  std::string format_with_commas(long int value)
  {
    std::string numWithCommas = std::to_string(value);
    int insertPosition = numWithCommas.length() - 3;
    while (insertPosition > 0)
    {
        numWithCommas.insert(insertPosition, ",");
        insertPosition-=3;
    }
    return numWithCommas;
  }
}


//----------------------------------------------------------------------------
// Initialize the transport module
// Includes setting up the grid, particles,
// and MPI work distribution
//----------------------------------------------------------------------------
void transport::init(ParameterReader* par, grid_general *g)
{
  params_  = par;
  grid = g;

  setup_MPI();

  // counts of memory being allocated
  int n_grid_variables = 0;
  int n_freq_variables = 0;

  // determine my zones to work on
  int nz = grid->n_zones;
  int blocks = floor(nz/MPI_nprocs);
  int remainder = nz - blocks*MPI_nprocs;

  int rcount = 0;
  for (int i=0;i<MPI_nprocs;i++)
  {
    int start = i*blocks + rcount;
    int stop  = start + blocks;
    if (rcount < remainder) { stop += 1; rcount += 1;}
    if (i == MPI_myID)
    {
      my_zone_start_ = start;
      my_zone_stop_  = stop;
    }
  }
  // arrays for communication
  src_MPI_block = new double[Max_MPI_Blocksize];
  dst_MPI_block = new double[Max_MPI_Blocksize];
  src_MPI_zones = new double[nz];
  dst_MPI_zones = new double[nz];
  n_grid_variables += 2;

//  std::cout << MPI_myID <<  " " << my_zone_start_ << " " << my_zone_stop_ <<
//   " " << my_zone_stop_ - my_zone_start_ << "\n";

  bool fix_seed = (bool) params_->getScalar<int>("transport_fix_rng_seed");
  unsigned long int seed_value = params_->getScalar<int>("transport_rng_seed");
  std::string restart_file = params_->getScalar<string>("run_restart_file");
  int do_restart = params_->getScalar<int>("run_do_restart");

  // setup and seed random number generator
  if (do_restart) {
    int status = rangen.readCheckpointRNG(restart_file);
    if (status != 0) {
      rangen.init(fix_seed, seed_value);
    }
  }
  else
    rangen.init(fix_seed, seed_value);

  // read relevant parameters
  max_total_particles = params_->getScalar<int>("particles_max_total");
  radiative_eq    = params_->getScalar<int>("transport_radiative_equilibrium");
  steady_state    = (params_->getScalar<int>("transport_steady_iterate") > 0);
  temp_max_value_ = params_->getScalar<double>("limits_temp_max");
  temp_min_value_ = params_->getScalar<double>("limits_temp_min");
  fleck_alpha_ = params_->getScalar<double>("transport_fleck_alpha");
  solve_Tgas_with_updated_opacities_ = params_->getScalar<int>("transport_solve_Tgas_with_updated_opacities");
  fix_Tgas_during_transport_ = params_->getScalar<int>("transport_fix_Tgas_during_transport");
  set_Tgas_to_Trad_ = params_->getScalar<int>("transport_set_Tgas_to_Trad");
  last_iteration_ = 0;


  // set temperature control parameters, check for conflicts
  if (radiative_eq != 0)
  {
    if (set_Tgas_to_Trad_ == 1)
	{
	  cerr << "# ERROR: radiative equilibrium turned on, set_Tgas_to_Trad_ cannot be set to 1.\n";
	  exit(1);
    }
  }

  if (solve_Tgas_with_updated_opacities_ == 1)
  {
    if (fix_Tgas_during_transport_ == 1 )
    {
	  cerr << "# ERROR: Cannot simultaneously set solve_Tgas_with_updated_opacities_ to 1 and fix_Tgas_during_transport_ to 1\n";
	  exit(1);
	}

    if (set_Tgas_to_Trad_ == 1)
	{
	  cout << "# WARNING: set_Tgas_to_Trad_ is set to 1, so this will override anything more detailed that might result from setting solve_Tgas_with_updateed_opacities to 1\n";
	}
  }

  if (fix_Tgas_during_transport_ == 1 && set_Tgas_to_Trad_ == 1)
  {
	cerr << "# ERROR: Cannot simultaneously set fix_Tgas_during_transport_ to 1 and set_Tgas_to_Trad_ == 1\n";
	exit(1);
  }


  // initialize the frequency grid
  int log_freq_grid = 0;
  std::vector<double> nu_dims = params_->getVector<double>("transport_nu_grid");
  if ((nu_dims.size() != 4)&&(nu_dims.size() != 3)) {
    cerr << "# improperly defined nu_grid; need {nu_1, nu_2, dnu, (log?)}; exiting" << endl;
    exit(1); }
  if (nu_dims.size() == 3)
    nu_grid_.init(nu_dims[0],nu_dims[1],nu_dims[2]);
  if (nu_dims.size() == 4)
  {
    if (nu_dims[3] == 1) {
        nu_grid_.log_init(nu_dims[0],nu_dims[1],nu_dims[2]);
        log_freq_grid = 1; }
    else nu_grid_.init(nu_dims[0],nu_dims[1],nu_dims[2]);
  }
  if (verbose)
  {
    std::cout << "# Frequency grid runs from nu = ";
    std::cout << nu_grid_.minval() << " Hz to " << nu_grid_.maxval() << " Hz" << std::endl;
    std::cout << "#    with " << nu_grid_.size() << " points";
    if (log_freq_grid)
      std::cout << " (logarithmically spaced)";
    std::cout << std::endl << "#" << std::endl;
  }

  if (do_restart)
    readCheckpointSpectra(restart_file);
  else {
    // intialize output spectrum
    std::vector<double>stg = params_->getVector<double>("spectrum_time_grid");
    std::vector<double>sng = params_->getVector<double>("spectrum_nu_grid");
    int nmu  = params_->getScalar<int>("spectrum_n_mu");
    int nphi = params_->getScalar<int>("spectrum_n_phi");
    optical_spectrum.init(stg,sng,nmu,nphi);
    std::vector<double>gng = params_->getVector<double>("gamma_nu_grid");
    gamma_spectrum.init(stg,sng,nmu,nphi);
  }
  escaped_particle_filename_ = params_->getScalar<string>("spectrum_particle_list_name");
  if (escaped_particle_filename_ == "")
    save_escaped_particles_ = 0;
  else
    save_escaped_particles_ = 1;
  maxn_escaped_particles_ = params_->getScalar<double>("spectrum_particle_list_maxn");

  // check if atomfile is there
  atomdata_file_ = params_->getScalar<string>("data_atomic_file");
  std::ifstream afile(atomdata_file_);
  if (!afile)
  {
    if (verbose)
      std::cerr << "Can't open atom datafile " << atomdata_file_ << "; exiting" << std::endl;
    exit(1);
  }
  afile.close();
  atomic_data_ = new AtomicData;
  atomic_data_->initialize(atomdata_file_,nu_grid_);

  // set max ion stage and levels to use
  int max_ion_stage = params_->getScalar<int>("data_max_ion_stage");
  if (max_ion_stage > 0)
    atomic_data_->set_max_ion_stage(max_ion_stage);
  int max_n_levels = params_->getScalar<int>("data_max_n_levels");
  if (max_n_levels > 0)
    atomic_data_->set_max_n_levels(max_n_levels);

  // setup the GasState class
#ifdef _OPENMP
  int max_nthreads = omp_get_max_threads();
#else
  int max_nthreads = 1;
#endif
  gas_state_vec_.resize(max_nthreads);

  int n_fuzzlines = 0;
  std::string fuzzfile = "";
  // Move the gas state parameter stuff to GasState::initialize
  for (auto i_gas_state = gas_state_vec_.begin(); i_gas_state != gas_state_vec_.end(); i_gas_state++) {
    // set gas opacity flags and parameters
    i_gas_state->use_electron_scattering_opacity
      = params_->getScalar<int>("opacity_electron_scattering");
    i_gas_state->use_line_expansion_opacity
      = params_->getScalar<int>("opacity_line_expansion");
    i_gas_state->use_fuzz_expansion_opacity
      = params_->getScalar<int>("opacity_fuzz_expansion");
    i_gas_state->use_bound_free_opacity
      = params_->getScalar<int>("opacity_bound_free");
    i_gas_state->use_bound_bound_opacity
      = params_->getScalar<int>("opacity_bound_bound");
    i_gas_state->use_free_free_opacity
      = params_->getScalar<int>("opacity_free_free");
    i_gas_state->use_user_opacity_
      = params_->getScalar<int>("opacity_user_defined");
    i_gas_state->bulk_grey_opacity_ = params_->getScalar<double>("opacity_grey_opacity");
    i_gas_state->use_zone_specific_grey_opacity_
      = params_->getScalar<int>("opacity_zone_specific_grey_opacity");
    double min_ext = params_->getScalar<double>("opacity_minimum_extinction");
    i_gas_state->set_minimum_extinction(min_ext);
    i_gas_state->atom_zero_epsilon_ = params_->getVector<int>("opacity_atom_zero_epsilon");
    i_gas_state->epsilon_           = params_->getScalar<double>("opacity_epsilon");

    // set non-lte settings
    use_nlte_ = params_->getScalar<int>("opacity_use_nlte");
    i_gas_state->use_collisions_nlte_ = params_->getScalar<int>("opacity_use_collisions_nlte");
    i_gas_state->no_ground_recomb = params_->getScalar<int>("opacity_no_ground_recomb");
    i_gas_state->initialize(atomic_data_,grid->elems_Z,grid->elems_A,nu_grid_);
    i_gas_state->set_atoms_in_nlte(params_->getVector<int>("opacity_atoms_in_nlte"));

    // getting fuzz line data
    fuzzfile = params_->getScalar<string>("data_fuzzline_file");
    n_fuzzlines = i_gas_state->read_fuzzfile(fuzzfile);

    // parameters for treatment of detailed lines
    line_velocity_width_ = params_->getScalar<double>("line_velocity_width");
    i_gas_state->line_velocity_width_ = line_velocity_width_;
  }

  if (verbose) gas_state_vec_[0].print_properties();

  maximum_opacity_ = params_->getScalar<double>("opacity_maximum_opacity");
  // define it as the first step, for NLTE
  first_step_ = 1;

  // set up outer inner boundary condition
  boundary_in_reflect_ = params_->getScalar<int>("transport_boundary_in_reflect");
  boundary_out_reflect_ = params_->getScalar<int>("transport_boundary_out_reflect");

  omit_composition_decay_ = params_->getScalar<int>("dont_decay_composition");

  // parameters for storing opacities
  omit_scattering_ = params_->getScalar<int>("opacity_no_scattering");
  store_Jnu_ = params_->getScalar<int>("transport_store_Jnu");
  // sanity check
  if ((!store_Jnu_)&&(use_nlte_))
    std::cerr << "WARNING: not storing Jnu while using NLTE; Bad idea!\n";

  // allocate memory for opacity/emissivity variables
  planck_mean_opacity_.resize(grid->n_zones);

  if (use_nlte_)
  {
    bf_heating.resize(grid->n_zones);
    ff_heating.resize(grid->n_zones);
    bf_cooling.resize(grid->n_zones);
    ff_cooling.resize(grid->n_zones);
    coll_cooling.resize(grid->n_zones);
  }

  rosseland_mean_opacity_.resize(grid->n_zones);
  n_grid_variables += 2;

  abs_opacity_.resize(grid->n_zones);
  if (!omit_scattering_) scat_opacity_.resize(grid->n_zones);
  emissivity_.resize(grid->n_zones);
  J_nu_.resize(grid->n_zones);
  n_freq_variables += 2;
  if (!omit_scattering_) n_freq_variables +=1;
  if (store_Jnu_) n_freq_variables += 1;

  for (int i=0; i<grid->n_zones;  i++)
  {
    // allocate absorptive opacity
    try {
      abs_opacity_[i].resize(nu_grid_.size()); }
    catch (std::bad_alloc const&) {
      cerr << "Memory allocation fail!" << std::endl; }

    // allocate scattering opacity
    if (!omit_scattering_)
    {
        try {
            scat_opacity_[i].resize(nu_grid_.size()); }
        catch (std::bad_alloc const&) {
            cerr << "Memory allocation fail!" << std::endl; }
    }

    // allocate emissivity
    emissivity_[i].resize(nu_grid_.size());

    // allocate Jnu (radiation field)
    if (store_Jnu_)
    {
      J_nu_[i].resize(nu_grid_.size());
    }
    else
      J_nu_[i].resize(1);
  }
  compton_opac.resize(grid->n_zones);
  photoion_opac.resize(grid->n_zones);
  n_grid_variables += 2;

  // setup emissivity weight  -- debug
  emissivity_weight_.resize(nu_grid_.size());
  double norm = 0;
  for (int j=0;j<nu_grid_.size();j++)
  {
    //double nu  = nu_grid.center(j);
    double w = 1.0; // - 1.0/(1.0 + pow(nu/3e15,2.0));
    //if (nu > 2e15) w = 100;
    emissivity_weight_[j] = w;
    norm += w;
  }
  for (int j=0;j<nu_grid_.size();j++) emissivity_weight_[j] *= nu_grid_.size()/norm;
 // for (int j=0;j<nu_grid_.size();j++) std::cout << nu_grid_.center(j) << " " << emissivity_weight_[j] << "\n";


 // ddmc parameters
 use_ddmc_ = params_->getScalar<int>("transport_use_ddmc");
 if (use_ddmc_)
 {
   ddmc_tau_ = params_->getScalar<double>("transport_ddmc_tau_threshold");
   ddmc_P_up_.resize(grid->n_zones);
   ddmc_P_dn_.resize(grid->n_zones);
   ddmc_P_adv_.resize(grid->n_zones);
   ddmc_P_abs_.resize(grid->n_zones);
   ddmc_P_stay_.resize(grid->n_zones);
   ddmc_use_in_zone_.resize(grid->n_zones);
   n_grid_variables += 6;

   if(use_ddmc_ == 3)
     setup_RandomWalk();

   if(verbose){
     std::cout << "# Using diffusion method "<<use_ddmc_<< " with threshold tau = ";
     std::cout << ddmc_tau_ << std::endl;
   }
 }

  // allocate space for emission distribution function across zones
  zone_emission_cdf_.resize(grid->n_zones);
  n_grid_variables += 1;

  // read pamaeters for core emission and setup
  setup_core_emission();

  // read parameters for pointsource emission and setup
  setup_pointsource_emission();

  // initialize time
  t_now_ = g->t_now;

  // initialize particles
  if (do_restart) {
    readCheckpointParticles(particles, restart_file, "particles");
    readCheckpointParticles(particles_escaped, restart_file, "particles_escaped");
  }
  else {
    int n_parts = params_->getScalar<int>("particles_n_initialize");
    initialize_particles(n_parts);
  }

  compton_scatter_photons_ = params_->getScalar<int>("opacity_compton_scatter_photons");
  if (compton_scatter_photons_)
    setup_MB_cdf(0.,5.,512); // in non-dimensional velocity units

  // print out memory footprint
  if (verbose)
  {
    long int nz = grid->n_zones;
    long int nfreq = nu_grid_.size()*grid->n_zones;
    long int osize = sizeof(OpacityType);

    std::cout << std::endl;
    std::cout << "# Estimated Memory usage for zone data";
    std::cout << std::endl;
    std::cout << "#---------------------------------------------------------|";
    std::cout << std::endl;
    std::cout << std::setw(10) << "#  data   |";
    std::cout << std::setw(9) << " # vars |";
    std::cout << std::setw(12) << " # pts |";
    std::cout << std::setw(9) << " each(B)|";
    std::cout << std::setw(18) << " total (B) |";
    std::cout << std::endl;
    std::cout << "#---------------------------------------------------------|";
    std::cout << std::endl;
    std::cout << std::setw(10) << "# zone    |";
    std::cout << std::setw(9) << format_with_commas(n_grid_variables) + " |";
    std::cout << std::setw(12) << format_with_commas(nz) + " |";
    std::cout << std::setw(9) << format_with_commas(sizeof(double)) + " |";
    std::cout << std::setw(18) << format_with_commas(n_grid_variables*nz*sizeof(double)) + " |";;
    std::cout << std::endl;
    std::cout << std::setw(10) << "# freq    |";
    std::cout << std::setw(9) << format_with_commas(n_freq_variables) + " |";
    std::cout << std::setw(12) << format_with_commas(nfreq) + " |";
    std::cout << std::setw(9) << format_with_commas(osize) + " |";
    std::cout << std::setw(18) << format_with_commas(n_freq_variables*nfreq*osize) +  " |";;
    std::cout << std::endl;
    std::cout << "#---------------------------------------------------------|";
    std::cout << std::endl;

    double zone_mem = n_grid_variables*nz*sizeof(double) + n_freq_variables*nfreq*osize;

    std::cout << "# Estimated Memory usage for atomic data" << std::endl;
    double atom_mem = gas_state_vec_[0].print_memory_footprint();

    double total_mem = zone_mem + atom_mem;
    std::cout << "# Estimated total memory usage > ";
    if (total_mem < 1e6)
      std::cout << total_mem/1e3 << " kB";
    else if (total_mem < 1e9)
      std::cout << total_mem/1e6 << " MB";
    else
      std::cout << total_mem/1e9 << " GB";


    std::cout << std::endl << "#" << std::endl;
    }


}

void transport::setup_MPI()
{
#ifdef MPI_PARALLEL
  // get mpi rank
  MPI_Comm_size( MPI_COMM_WORLD, &MPI_nprocs );
  MPI_Comm_rank( MPI_COMM_WORLD, &MPI_myID  );
  MPI_real = ( sizeof(SedonaReal)==4 ? MPI_FLOAT : MPI_DOUBLE );
#else
  MPI_nprocs = 1;
  MPI_myID= 0;
#endif
  verbose = (MPI_myID==0);
}


void transport::setup_MB_cdf(double min_v, double max_v, int num_v)
{
  mb_cdf_.resize(num_v);
  mb_dv = (max_v - min_v)/( (double) num_v);

  //setup the cdf
  double v = 0;
  for (int j = 0; j < num_v; j++)
    {
      v += mb_dv;
      mb_cdf_.set_value(j,4./(sqrt(pc::pi)) * pow(v,2.) * exp(-pow(v,2.)));
    }
  mb_cdf_.normalize();


}


// -----------------------------------------------------------
// Read parameters for a spherical emitting core and
// setup the emission
// -----------------------------------------------------------
void transport::setup_core_emission()
{

  // -----------------------------------------------------------
  // set up inner boundary emission properties
  r_core_         = params_->getScalar<double>("core_radius");
  T_core_         = params_->getScalar<double>("core_temperature");
  core_frequency_ = params_->getScalar<double>("core_photon_frequency");
  L_core_         = params_->getFunction("core_luminosity", 0);
  time_core_      = params_->getScalar<double>("core_timescale");
  core_fix_luminosity_ = params_->getScalar<int>("core_fix_luminosity");

  // set blackbody from L and R if appropriate
  if ((L_core_ !=0)&&(r_core_ != 0)&&(T_core_ == 0))
    T_core_ = pow(L_core_/(4.0*pc::pi*r_core_*r_core_*pc::sb),0.25);

  int total_n_emit    = params_->getScalar<int>("core_n_emit");
  if (total_n_emit > 0)
  {
    // allocate and set up core emission spectrum
    core_emission_spectrum_.resize(nu_grid_.size());

    std::string core_spectrum_filename = params_->getScalar<string>("core_spectrum_file");
    vector<double> cspec_nu, cspec_Lnu;
    if (core_spectrum_filename != "")
    {
     double x1,x2;
      std::ifstream specfile;
      specfile.open(core_spectrum_filename.c_str());
      if (!specfile.is_open())
      {
        if (verbose) std::cerr << "Can't open core_spectrum_file " << core_spectrum_filename << endl;
        core_spectrum_filename = "";
      }
      else while (!specfile.eof( ))
      {
        specfile >> x1;
        specfile >> x2;
        cspec_nu.push_back(x1);
        cspec_Lnu.push_back(x2);
     }
   }

    // set up emission spectrum
   double L_sum = 0;
   for (int j=0;j<nu_grid_.size();j++)
   {
      double nu  = nu_grid_.center(j);
      double dnu = nu_grid_.delta(j);

      // read in spectrum
      if (core_spectrum_filename != "")
      {
        double Lnu;
        int ind = lower_bound(cspec_nu.begin(),cspec_nu.end(),nu)- cspec_nu.begin() - 1;
        if (ind < 0) Lnu = 0;
        else if ((size_t)ind >= cspec_nu.size()-1) Lnu = 0;
       else
       {
          //double slope = 0; //(cspec_Lnu[ind+1] - cspec_Lnu[ind-1])/(cspec_nu[ind+1] - cspec_nu[ind]);
          Lnu = cspec_Lnu[ind]; // + slope*(nu - cspec_nu[ind]);
        }
        core_emission_spectrum_.set_value(j,Lnu*dnu*emissivity_weight_[j]);
       L_sum += Lnu*dnu;
      }
      else
      // blackbody spectrum
     {
        double bb;
        if (T_core_ <= 0) bb = 1;
        else bb = blackbody_nu(T_core_,nu);
        core_emission_spectrum_.set_value(j,bb*dnu*emissivity_weight_[j]);
        // blackbody flux is pi*B(T)
       L_sum += 4.0*pc::pi*r_core_*r_core_*pc::pi*bb*dnu;
      }
    }
   core_emission_spectrum_.normalize();
   if (L_core_ == 0) L_core_ = L_sum;

    if (verbose)
    {
      if (core_spectrum_filename != "")
        cout << "# Inner source luminosity (at t = 0) = " << L_core_ <<
       " erg/s, read from file " << core_spectrum_filename << "\n";
      else
        cout << "# Inner source luminosity = " << L_core_ <<
        "  erg/s, from a blackbody T = " << T_core_ << "\n";
    }
  }
  // -----------------------------------------------------------

}


// -----------------------------------------------------------
// Read parameters for multiple emitting point sources
// and etup the emission
// -----------------------------------------------------------
void transport::setup_pointsource_emission()
{
  std::string ps_filename = params_->getScalar<string>("particles_pointsource_file");
  use_pointsources_ = 0;
  if (ps_filename == "") return;

  std::ifstream ps_file;
  ps_file.open(ps_filename.c_str());
  if (!ps_file.is_open())
  {
    if (verbose) std::cerr << "Can't open point source file " << ps_filename << endl;
    return;
  }

  while (!ps_file.eof( ))
  {
    use_pointsources_ = 1;

    double xr[3], L, T;
    ps_file >> xr[0];
    ps_file >> xr[1];
    ps_file >> xr[2];
    ps_file >> L;
    ps_file >> T;
    if (ps_file.eof()) break;
    pointsource_x_.push_back(xr[0]);
    pointsource_y_.push_back(xr[1]);
    pointsource_z_.push_back(xr[2]);
    pointsource_L_.push_back(L);
    pointsource_T_.push_back(T);
  }

  pointsources_L_tot_ = 0;
  int n_sources = (int)pointsource_T_.size();
  pointsource_emission_cdf_.resize(n_sources);
  for (int i=0;i<n_sources;i++)
  {
    pointsources_L_tot_ += pointsource_L_[i];
    pointsource_emission_cdf_.set_value(i,pointsource_L_[i]);
  }
  pointsource_emission_cdf_.normalize();

  // setup emission spectrum
  pointsource_emission_spectrum_.resize(nu_grid_.size());
  for (int j=0;j<nu_grid_.size();j++)
  {
    double nu  = nu_grid_.center(j);
    double dnu = nu_grid_.delta(j);
    double bb = blackbody_nu(T_core_,nu);
    pointsource_emission_spectrum_.set_value(j,bb*dnu*emissivity_weight_[j]);
  }
  pointsource_emission_spectrum_.normalize();

  if (verbose)
  {
    std::cout << "# From pointsource file: " << ps_filename << "\n";
    std::cout << "#   Read " << n_sources << " pointsources; L_tot = " << pointsources_L_tot_ << "\n";
  }
}
