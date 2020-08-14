sedona_home   = os.getenv('SEDONA_HOME')
defaults_file    = sedona_home.."/defaults/sedona_defaults.lua"
data_atomic_file = "./atom_data.hdf5"
model_file    = "onezone.mod"       

transport_nu_grid  = {3.5e13,3.e17,0.005,1}  -- frequency grid
transport_steady_iterate         = 0
transport_radiative_equilibrium  = 1
transport_boundary_in_reflect    = 1
transport_boundary_out_reflect   = 1

transport_solve_Tgas_with_updated_opacities = 1

tstep_max_steps    = 3
tstep_time_stop    = 300.
tstep_max_dt       = 0.01
tstep_min_dt       = 0.01
tstel_max_delta    = 1.0


-- inner source emission
particles_max_total  = 1e8
core_n_emit          = 0
core_radius          = 1.0e14
particles_n_emit_thermal  = 0
particles_n_initialize  = 1e8

-- output spectrum
spectrum_nu_grid   		    = transport_nu_grid
output_write_atomic_levels	= 1
output_write_radiation		= 1
output_write_plt_file_time  = 0.01

-- opacity information
opacity_grey_opacity  = 0

opacity_use_nlte      = 1
opacity_atoms_in_nlte = {1,2,6,7,8,26}
opacity_bound_bound   = 1
opacity_bound_free    = 1
opacity_free_free     = 1
opacity_electron_scattering = 1
line_velocity_width   = 5e7

limits_temp_min = 5000.








