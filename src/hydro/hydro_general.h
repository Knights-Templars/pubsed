#ifndef _HYDRO_GENERAL_H
#define _HYDRO_GENERAL_H

#include "grid_general.h"
#include "ParameterReader.h"

class hydro_general
{

 private:



 public:

  hydro_general()
  {
    gamfac_ = 1.666666;
    cfl_    = 0.1;
    use_transport_ = 0;
  }

  int use_transport_;

  grid_general *grid;
  double t_start;

  double gamfac_;
  double cfl_;
  double mean_particle_mass_; // in atomic mass units. In the future, should couple to NLTE for each zone

  virtual void init(ParameterReader*, grid_general*) = 0;
  virtual double get_time_step() = 0;
  virtual void step(double)      = 0;
  virtual void evolve_to_start(double, int)
  {
  };

};

#endif
