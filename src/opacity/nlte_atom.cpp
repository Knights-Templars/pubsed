#include <limits>
#include "nlte_atom.h"
#include "physical_constants.h"
#include <stdlib.h>
#include <stdio.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_permutation.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_multiroots.h>
#include <gsl/gsl_linalg.h>
#include <iostream>

using namespace std;

// ---------------------------------------------------
// For the NLTE problem, we are solving a matrix equation
// M x = b
// where 
//   x is the vector of the level population fractions
//   M is the rate matrix
//   and b is the zero vector assuming statistical equilibrium.
//
// the number density in each level is n_i = x_i*n_tot
// where n_tot is the total number density of the species
//
// Note: one of the rate equations is not independent, 
// so in order for the matrix to be non-singular, we need to 
// make the last equation express number conservation
//   sum_i  x_i = 1
// ---------------------------------------------------

namespace pc = physical_constants;

nlte_atom::nlte_atom()
{
  e_gamma = 0;
  no_ground_recomb = 0;
  use_betas = 0;
}



void nlte_atom::solve_lte(double T, double ne, double time)
{

  // calculate partition functions
  for (int i=0;i<n_ions;i++) ions[i].part = 0;
  for (int i=0;i<n_levels;i++)
  {
    levels[i].n = levels[i].g*exp(-levels[i].E/pc::k_ev/T);
    ions[levels[i].ion].part += levels[i].n;
  }

  // thermal debroglie wavelength, lam_t**3
  double lt = pc::h*pc::h/(2.0*pc::pi*pc::m_e*pc::k*T);
  double fac = 2/ne/pow(lt,1.5);

  // calculate saha ratios
  ions[0].frac = 1.0;
  double norm  = 1.0;
  for (int i=1;i<n_ions;i++) 
  {
    // calculate the ratio of i to i-1
    double saha = exp(-1.0*ions[i-1].chi/pc::k_ev/T);
    saha = saha*(ions[i].part/ions[i-1].part)*fac;
    
    // set relative ionization fraction
    ions[i].frac = saha*ions[i-1].frac;

    // check for ridiculously small numbers
    if (ne < 1e-50) ions[i].frac = 0;
    norm += ions[i].frac;
  }
  // renormalize ionization fractions
  for (int i=0;i<n_ions;i++) ions[i].frac = ions[i].frac/norm;
  
  // calculate level densities (bolztmann factors)
  for (int i=0;i<n_levels;i++)
  {
    double E = levels[i].E;
    int    g = levels[i].g;
    double Z = ions[levels[i].ion].part;
    double f = ions[levels[i].ion].frac;
    levels[i].n = f*g*exp(-E/pc::k_ev/T)/Z;
    levels[i].n_lte = levels[i].n;
    levels[i].b = 1;
  }

}





//-------------------------------------------------------
//
//------------------------------------------------------
void nlte_atom::set_rates(double T, double ne)
{
  // zero out rate matrix
  for (int i=0;i<n_levels;i++)
    for (int j=0;j<n_levels;j++)
      rates[i][j] = 0;

  // ------------------------------------------------
  // radiative bound-bound transitions
  // ------------------------------------------------
  for (int l=0;l<n_lines;l++)
  {
    int lu     = lines[l].lu;
    int ll     = lines[l].ll;

    // spontaneous dexcitation + stimulated emission
    double R_ul = lines[l].B_ul*lines[l].J + lines[l].A_ul;
    double R_lu = lines[l].B_lu*lines[l].J;

    // add in escape probability suppresion
    if (this->use_betas) 
    {
      R_ul *= lines[l].beta;
      R_lu *= lines[l].beta; 
    }

    // add into rates
    rates[ll][lu] += R_lu;
    rates[lu][ll] += R_ul;

    //printf("RR %d %d %e\n",ll,lu,R_lu);
    //printf("RR %d %d %e\n",lu,ll,R_ul);
  }

 // ------------------------------------------------
  // non-thermal (radioactive) bound-bound transitions
  // ------------------------------------------------
  double norm = 0;
  for (int l=0;l<n_lines;l++) norm   += lines[l].f_lu;

  for (int l=0;l<n_lines;l++)
  {
    int lu  = lines[l].lu;
    int ll  = lines[l].ll;

    double dE = (levels[lu].E - levels[ll].E)*pc::ev_to_ergs;
    double R_lu = e_gamma/n_dens/dE; //*(lines[l].f_lu/norm);

    if (ll != 0) R_lu = 0;

    // add into rates
    rates[ll][lu] += R_lu;

    //printf("GR %d %d %e %e %e\n",ll,lu,R_lu,e_gamma,dE);
  }


  // ------------------------------------------------
  // collisional bound-bound transitions
  // ------------------------------------------------
  for (int i=0;i<n_levels;i++)
    for (int j=0;j<n_levels;j++)
    {
      // skip transitions to same level
      if (i == j) continue;
      // skip if to another ionization state (not bound-bound)
      if (levels[i].ion != levels[j].ion) continue;

      // level energy difference (in eV)
      double dE = levels[i].E - levels[j].E;
      double zeta = dE/pc::k_ev/T;
      // make sure zeta is positive
      if (zeta < 0) zeta = -1*zeta;

      // rate for downward transition: u --> l
      double C = 2.16*pow(zeta,-1.68)*pow(T,-1.5); // f_ul 

      // rate if it is a upward transition: l --> u
      if (dE < 0)
      {
	// use condition that collision rates give LTE
	double gl = levels[i].g;
	double gu = levels[j].g;
	C = C*gu/gl*exp(-zeta);      
      }

      // add into rates
      rates[i][j] += C;

      //printf("CR: %d %d %e\n",i,j,C);
    }


  // ------------------------------------------------
  // bound-free transitions
  // ------------------------------------------------
  for (int i=0;i<n_levels;i++)
  {
    int ic = levels[i].ic;
    if (ic == -1) continue;

    // ionization potential
    int istage  = levels[i].ion;
    double chi  = ions[istage].chi - levels[i].E;
    double zeta = chi/pc::k_ev/T;

    // collisional ionization rate
    double C_ion = 2.7/zeta/zeta*pow(T,-1.5)*exp(-zeta)*ne;
    rates[i][ic] += C_ion;

    // collisional recombination rate
    int gi = levels[i].g;
    int gc = levels[ic].g;
    double C_rec = 5.59080e-16/zeta/zeta*pow(T,-3)*gi/gc*ne*ne;
    rates[ic][i] += C_rec;

    // radiative recombination rate (debug)
    double R_rec = ne*levels[i].a_rec.value_at(T);
    // suppress recombinations to ground  
    if (no_ground_recomb) if (levels[i].E == 0) R_rec = 0;
    rates[ic][i] += R_rec;

    // photoionizaiton rate (debug used fixed J here)
    double W = 1.0;
    double R_ion = 0;
    for (int j=1;j<levels[i].s_photo.size();j++)
    {
      double  E    = levels[i].s_photo.x[j];
      double nu    = E*pc::ev_to_ergs/pc::h;
      double  E_0  = levels[i].s_photo.x[j-1];
      double nu_0  = E_0*pc::ev_to_ergs/pc::h;
      double dnu   = (nu - nu_0);
      double J     = W*blackbody_nu(T,nu);
      double sigma = levels[i].s_photo.y[j];
      // correction for stimulated recombination
      sigma = sigma*(1 - exp(-pc::h*nu/pc::k/T));
      R_ion += 4*pc::pi*sigma*J/(pc::h*nu)*dnu; 
    }

    // suppress ionization froms ground  NOT
    //if (levels[i].E == 0) {R_ion = 0; }
    rates[i][ic] += R_ion;
    
    //printf("pc::pi: %d %d %e %e\n",i,ic,R_rec,R_ion);
    //printf("CI: %d %d %e %e\n",i,ic,C_rec,C_ion);
    
  }

  // multiply by rates by lte pop in level coming from
  // (becuase we will solve for depature coeffs)
  for (int i=0;i<n_levels;i++)
      for (int j=0;j<n_levels;j++)
	rates[i][j] *= levels[i].n_lte;

  // print out rates if you so like
  //printf("------- rates ----------\n");
  //for (int i=0;i<n_levels;i++)
  //  for (int j=0;j<n_levels;j++)
  //    printf("%5d %5d %14.5e\n",i,j,rates[i][j]);
  // printf("\n");

}

int nlte_atom::solve_nlte(double T,double ne,double time)
{
  // initialize with LTE populations
  // this will also calculate line taus and betas
  solve_lte(T,ne,time);

  // debug; I'm going to set line J's as BB
  for (int i=0;i<n_lines;i++)
  {
    double nu  = lines[i].nu;
    double W  = 1.0;
    lines[i].J = W*blackbody_nu(T,nu);
  }

  int max_iter = 100;

  // iterate betas
  for (int iter = 0; iter < max_iter; iter++)
  {
    // Set rates
    set_rates(T,ne);

    // zero out matrix and vectors
    gsl_matrix_set_zero(M_nlte);
    gsl_vector_set_zero(b_nlte);
    gsl_vector_set_zero(x_nlte);
    gsl_permutation_init(p_nlte);

    // set up diagonal elements of rate matrix
    for (int i=0;i<n_levels;i++) 
    {
      double Rout = 0.0;
      // don't worry i = j rate should be zero
      for (int j=0;j<n_levels;j++) Rout += rates[i][j];
      Rout = -1*Rout;
      gsl_matrix_set(M_nlte,i,i,Rout);
    }
    
    // set off diagonal elements of rate matrix
    for (int i=0;i<n_levels;i++) 
      for (int j=0;j<n_levels;j++)
	if (i != j) gsl_matrix_set(M_nlte,i,j,rates[j][i]);
    
    // last row expresses number conservation
    for (int i=0;i<n_levels;i++) 
      gsl_matrix_set(M_nlte,n_levels-1,i,levels[i].n_lte);
    gsl_vector_set(b_nlte,n_levels-1,1.0);

    //printf("----\n");
    //for (int i=0;i<n_levels;i++) 
    //  for (int j=0;j<n_levels;j++)
    //   printf("%5d %5d %14.3e\n",i,j,gsl_matrix_get(M_nlte,i,j));
    // printf("----\n");
    
    // solve matrix
    int status;
    gsl_linalg_LU_decomp(M_nlte, p_nlte, &status);
    gsl_linalg_LU_solve(M_nlte, p_nlte, b_nlte, x_nlte);

    // the x vector should now have the solved level 
    // depature coefficients
    for (int i=0;i<n_levels;i++) 
    {
      double b = gsl_vector_get(x_nlte,i);
      double n_nlte = b*levels[i].n_lte;
      levels[i].n = n_nlte;
      levels[i].b = b;
      //      printf("%d %e %e\n",i,levels[i].b,levels[i].n/levels[i].n_lte);
    }

    // set the ionization fraction
    for (int i=0;i<n_ions;i++) ions[i].frac = 0;
    for (int i=0;i<n_levels;i++)
      ions[levels[i].ion].frac += levels[i].n;

    if (!this->use_betas) return 1;

    // see if the betas have converged
    int converged   = 1;
    double beta_tol = 0.1;
    for (int i=0; i<n_lines; i++)
    { 
      double old_beta = lines[i].beta;
      compute_sobolev_tau(i,time);
      double new_beta = lines[i].beta;
      
      if (fabs(old_beta - new_beta)/new_beta > beta_tol) 
	converged = 0;
    }
    if (converged) {return 1; }
    //printf("-----\n");
  }

  printf("# NLTE not converging\n");
  return 0;

}

double nlte_atom::get_ion_frac()
{
  double x = 0;
  for (int i=0;i<n_levels;i++)
    x += levels[i].n*levels[i].ion;
  return x;
}

void nlte_atom::compute_sobolev_taus(double time)
{
  for (int i=0;i<n_lines;i++) compute_sobolev_tau(i,time);
}

double nlte_atom::compute_sobolev_tau(int i, double time)
{
  int ll = lines[i].ll;
  int lu = lines[i].lu;

  double nl = levels[ll].n;
  double nu = levels[lu].n;
  double gl = levels[ll].g;
  double gu = levels[lu].g;

  // check for empty levels
  if (nl < std::numeric_limits<double>::min())
  { 
    lines[i].tau  = 0;
    lines[i].etau = 1;
    lines[i].beta = 1;
    return 0;
  }

  double lam   = pc::c/lines[i].nu;
  double tau   = nl*n_dens*pc::sigma_tot*lines[i].f_lu*time*lam;
  // correction for stimulated emission
  tau = tau*(1 - nu*gl/(nl*gu));

  if (nu*gl > nl*gu) {
    printf("laser regime, line %d, whoops\n",i);
    lines[i].tau  = 0;
    lines[i].etau = 1;
    lines[i].beta = 1;
    return 0; }

  double etau = exp(-tau);
  lines[i].etau = etau;
  lines[i].tau  = tau;
  lines[i].beta = (1-etau)/tau;
  return lines[i].tau;
}



double nlte_atom::Calculate_Milne(int lev, double temp)
{
  // Maxwell-Bolztmann constants
  double v_MB = sqrt(2*pc::k*temp/pc::m_e);
  double MB_A = 4/sqrt(pc::pi)*pow(v_MB,-3);
  double MB_B = pc::m_e/pc::k/2.0/temp;
  double milne_fac = pow(pc::h/pc::c/pc::m_e,2);

  // starting values
  double sum   = 0;
  double nu_t  = levels[lev].E_ion*pc::ev_to_ergs/pc::h;
  double nu    = nu_t;
  double vel   = 0; 
  double fMB   = 0; 
  double sigma = 0; 
  double coef  = 0; 
  double old_vel  = vel;
  double old_coef = coef;

  // integrate over velocity/frequency
  for (int i=1;i<levels[lev].s_photo.size();i++)
  {
    // recombination cross-section
    double E = levels[lev].s_photo.x[i];
    double S = levels[lev].s_photo.y[i];
    nu       = E*pc::ev_to_ergs/pc::h;
    vel      = sqrt(2*pc::h*(nu - nu_t)/pc::m_e);
    if (nu < nu_t) vel = 0;
    fMB   = MB_A*vel*vel*exp(-MB_B*vel*vel);
    sigma = milne_fac*S*nu*nu/vel/vel;
    coef  = vel*sigma*fMB;

    // integrate
    sum += 0.5*(coef + old_coef)*(vel - old_vel);
    // store old values
    old_vel  = vel;
    old_coef = coef;
  }
  
  // ionize to state
  int ic = levels[lev].ic;
  if (ic == -1) return 0;

  // return value
  //return levels[lev].g/levels[ic].g*sum;
  return (1.0*levels[lev].g)/(1.0*levels[ic].g)*sum;
}


void nlte_atom::print()
{

  cout << "--------------------- ions; n = " << n_ions << " ---------------------\n";
  cout << "# ion \t part \t frac \t chi (eV)\n";
  cout << "#---------------------------------------------------------------\n";

  
  for (int i=0;i<n_ions;i++)
    cout << "#   "<<  ions[i].stage << "\t" << ions[i].part << "\t" << ions[i].frac 
	 << "\t" << ions[i].chi << endl;
 

  cout << "\n";
  cout << "--------------------------------------------------------------------\n";
  cout << "--------------------levels; n = " << n_levels << "------------------------\n";
  cout << "# lev   ion     E_ex        g      pop          b_i       ion_to\n";
  cout << "#---------------------------------------------------------------\n";

  for (int i=0;i<n_levels;i++)
  {
    printf("%5d %4d %12.3e %5d %12.3e %12.3e %5d\n",
	   levels[i].globalID,levels[i].ion,
	   levels[i].E,levels[i].g,levels[i].n,
	   levels[i].b,levels[i].ic);
  }

  printf("\n--- line data\n");

  for (int i=0;i<n_lines;i++)
  {
    printf("%8d %4d %4d %12.3e %12.3e %12.3e %12.3e %12.3e\n",
    	   i,lines[i].ll,lines[i].lu,lines[i].nu,lines[i].f_lu,
    	   lines[i].A_ul,lines[i].B_ul,lines[i].B_lu);
  }

  printf("\n--- line optical depths\n");

  for (int i=0;i<n_lines;i++)
  {
    int    ll = lines[i].ll;
    double nl = levels[ll].n;

    printf("%8d %4d %4d %12.3e %12.3e %12.3e\n",
    	   i,lines[i].ll,lines[i].lu,lines[i].nu,
	   lines[i].tau,nl);
  }


}

//-----------------------------------------------------------------
// calculate planck function in frequency units
//-----------------------------------------------------------------
double nlte_atom::blackbody_nu(double T, double nu)
{
  double zeta = pc::h*nu/pc::k/T;
  return 2.0*nu*nu*nu*pc::h/pc::c/pc::c/(exp(zeta)-1);
}
