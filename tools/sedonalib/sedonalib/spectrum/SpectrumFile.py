import numpy as np
import h5py
import physical_constants as pc
from numpy import newaxis
from Filter import *
from scipy.interpolate import interp1d
from scipy import integrate as integrate

class SpectrumFile():
    """
    Class for reading in and handling spectrum files output
    by the Sedona code

    Allowed units:
    spec_units: 'hz','angstrom','cm'
    time_units: 'day','sec'

    The spectrum data is stored as a 4D array of the form
    L[n_times, n_freq, n_mu, n_phi]
    where L is the specific luminosity (ergs/s/Hz or ergs/s/Ang)

    """

    def __init__(self,name,spec_units=None,time_units='day',verbose=True):

        self.verbose = verbose
        self.filename = name

        allowed_spec_units = ['angstrom','hz','cm','micron']
        allowed_time_units = ['sec','day']

        # default spectral units
        if (spec_units is None):
            spec_units = 'hz'

        # default time unit
        if (time_units is None):
            time_units = 'day'

        self.spec_units = spec_units
        if (self.spec_units not in allowed_spec_units):
            if (verbose):
                print ('ERROR: unknown spectrum units; using default ')
            self.spec_units = 'hz'

        # set the time units
        if (time_units == "s"):
            time_units = "sec"
        if (time_units == "days"):
            time_units = "day"
        self.time_units = time_units
        if (self.time_units not in allowed_time_units):
            if (verbose):
                print ('ERROR: unknown time units; using default = day')
            self.time_units = 'day'

        self.filter = Filter()

        self.Lbol = None
        self.read_data()


    # was hoping this would keep user from changing stuff...
    @property
    def mu(self):
        return self.mu

    @property
    def shape(self):
        return self.L.shape

    def read_data(self):
        """
        Read data in from a spectrum hdf5 file
        """

        fin = h5py.File(self.filename,"r")
        self.nu     = np.array(fin['nu'])
        self.L     = np.array(fin['Lnu'])
        self.t         = np.array(fin['time'])
        try:
            self.mu  = np.array(fin['mu'])
        except:
            self.mu  = np.zeros(1)
        try:
            self.phi       = np.array(fin['phi'])
        except:
            self.phi = np.zeros(1)

        self.lam = pc.c/self.nu*pc.cm_to_angs
        # default is nu
        self.x = self.nu


# Stuff that may not be defined
 #       self.click = np.array(fin['click'])
    #    self.phi_edges = np.array(fin['phi_edges'])
   #     self.mu_edges  = np.array(fin['mu_edges'])
  #      self.t_edges   = np.array(fin['time_edges'])

        fin.close()

        self.n_x = len(self.x)
        self.n_times = len(self.t)
        self.n_mu = len(self.mu)
        self.n_phi = len(self.phi)
#        self.n_phi = 1

        # reshape so we always have 4 dimensions
        if (self.n_mu == 1 and self.n_phi == 1):
            self.L = self.L[..., newaxis,newaxis]
        if (self.n_mu > 1 and self.n_phi == 1):
            self.L = self.L[..., newaxis]


        if (self.spec_units == 'angstrom' and self.n_x > 1):

            for it,im,ip in np.ndindex(self.n_times,self.n_mu,self.n_phi):
                # change units of luminosity to erg/s/A
                newL = self.L[it,:,im,ip]*self.x**2/pc.c/pc.cm_to_angs
                # reverse the order fo the array
                self.L[it,:,im,ip]  = newL[::-1]

            # change x units to wavelength and flip order
            newx  = pc.c/self.x*pc.cm_to_angs
            self.x = newx[::-1]

        if (self.time_units == 'day'):
            self.t       = self.t*pc.sec_to_day
 #           self.t_edges = self.t_edges*pc.sec_to_day


    def __str__(self):

        val =  "Spectrum from file = " + self.filename
        val += "\n\ntime pts = " + str(len(self.t))
        val += "\n freq pts = " + str(len(self.x))
        val += "\n   mu pts = " + str(len(self.mu))
        val += "\n  phi pts = " + str(len(self.phi))

        return val


    def print_filters(self):
        self.filter.list()

    def switch_units(self,spec_units='angstroms'):

        new_spec_units = spec_units

    ## return functions

    def get_nu(self):
        return self.nu
    def get_nu_range(self):
        return [min(self.nu),max(self.nu)]

    def get_lambda(self):
        return self.lam
    def get_lambda_range(self):
        return [min(self.lam),max(self.lam)]

    def get_time(self):
        return self.t

    def view_lightcurve(self,thisL,view=None):
        """
        Function takes in a light curve of format
        L[ntimes,nmu,nphi] and returns one viewed from angle 'view'
        If 'view'
        """
        print 'shape = ',thisL.shape

        # if spherically symmetric, just return time-dependent lc
        if (self.n_mu == 1 and self.n_phi == 1):
            return self.t,thisL[:,0,0]

        # if viewing angle not specified, return the full thing
        if (view is None):
            if (self.n_mu > 1 and self.n_phi == 1):
                return self.t,thisL[:,:,0]
            if (self.n_mu == 1 and self.n_phi > 1):
                return self.t,thisL[:,0,:]
            return self.t,thisL


        # interpolate to particular viewing angle
        # need to implement



    def get_lightcurve(self,band=None,magnitudes=None,view=None):
        """
        General function to return a light curve in a given band

        if 'band' is not passed, returns the bolometric light curve

        Parameters
        ----------
        band : str or wavelength region

        view : array --
            The viewing angle, in the format [theta,phi] or just [theta]
            Will interpolate to that viewing angle

        magnitudes : str, optional
            The magnitude system to use, if any
            allowed values are
                None -- will return Lnu
                "nuFnu" -- will return Lnu*nu_mean
                "AB" -- AB magnitude system
                "vega" -- vega system

        """

        # if no band specified, then return bolometric light curve
        if (band is None):
            lc = self.get_bolometric_lightcurve()


        # if band is a string, the return the lightcurve in that filter
        if (isinstance(band,str)):

            if (band == "bol" or band == "bolometric"):
                lc = self.get_bolometric_lightcurve()
            else:
                lc = self.get_band_lightcurve(band,magnitudes=magnitudes,view=view)

        # if band is a list of two numbers, then return the lightcurve
        # in that filter
        if (isinstance(band,list)):
            print band
            if (len(band) == 2):
                lc = self.get_range_lightcurve(band,magnitudes=magnitudes,view=view)

        return lc


    def get_bolometric_lightcurve(self,view=None,magnitudes=None):
        """
        Returns the bolometric light curve of the spectrum
        Stores value in Lbol
        """

        # compute bolometric light curve, store it locally
        if (self.Lbol is None):
            self.Lbol = np.zeros([self.n_times,self.n_mu,self.n_phi],dtype='d')
            self.Lave = np.zeros([self.n_times])

        # integrate bolometric light curve
        for it,im,ip in np.ndindex(self.n_times,self.n_mu,self.n_phi):
            if (self.n_x == 1):
                self.Lbol[it,im,ip] = self.L[it,0,im,ip]
                self.Lave[it] += self.L[it,0,im,ip]
            else:
                self.Lbol[it,im,ip] = np.trapz(self.L[it,:,im,ip],x=self.x)
                self.Lave[it] += self.Lbol[it,im,ip]

                self.Lave = self.Lave/(1.0*self.n_mu*self.n_phi)

        Lbol = self.Lbol
        Lave = self.Lave

        #thisL = self.Lbol
        #if (angle_average):
        #    thisL = self.Lave

        thisL = self.Lbol
        if (magnitudes is not None):
            thisL = -2.5*np.log10(thisL)+88.697425

        return self.view_lightcurve(thisL,view)


    def get_band_lightcurve(self,band,magnitudes=None, view=None):
        """
        returns the light curve in a given filter band

        Parameters
        ----------
        band : str
            name of the filter to calculate the light curve in
        magnitudes :str

        """
        filt_norm = self.filter.getNormalization_nu(band,self.x)

        print magnitudes
        filt_trans = self.filter.transFunc_nu(band)(self.x)

        print filt_norm

        L_return = np.zeros([self.n_times,self.n_mu,self.n_phi],dtype='d')
        for it,im,ip in np.ndindex(self.n_times,self.n_mu,self.n_phi):

            if (magnitudes is None):
                thisL = self.L[it,:,im,ip]
            else:
                thisL = self.L[it,:,im,ip]/self.x


            sample_points = thisL*filt_trans
            L_return[it,im,ip] = integrate.trapz(sample_points,x=self.x)

            if (magnitudes is not None):
                L_return[it,im,ip] /= filt_norm

        if (magnitudes == "AB"):

            print 'hi'
            # convert to flux at 10 parsecs
            L_return /= (4.*np.pi*(10.*3.0857e18)**2)

            #set 0's to some small number so not taking log(0)
            L_return[np.where(L_return==0.)] = 1e-99

            #get the AB magnitude
            L_return = -2.5*np.log10(L_return)-48.6

            #set minimum mag to 0
            L_return[np.where(L_return>0)] = 0.


        return self.view_lightcurve(L_return,view)



    def get_range_lightcurve(self,range,magnitudes=None, view=None):
        """
        Returns the lightcurve integrated over the wavelength region
        specified in 'range'

        Parameters
        -----------

        """

        print range

        thisL = np.zeros([self.n_times,self.n_mu,self.n_phi],dtype='d')

        # define region to integrate over
        b = (self.x >= range[0])*(self.x <= range[1])
        if (sum(b) == 0):
            message = "Wavelength range for band light curve not in spectrum range"
            raise ValueError(message)

        # integrate light curve
        for it,im,ip in np.ndindex(self.n_times,self.n_mu,self.n_phi):
            thisL[it,im,ip] = np.trapz(self.L[it,b,im,ip],x=self.x[b])

        if (magnitudes == "Lnu" or magnitudes == "Llam"):
            thisL = thisL/(range[1] - range[0])

        return self.view_lightcurve(thisL,view)



###############
    def plot_lightcurve(self,band=None,magnitudes=None,view=None,xrange=None,yrange=None,logx=None,logy=None,logxy=None):
        """
        Make a simple plot of the light curve
        """

        print magnitudes
        import matplotlib.pyplot as plt

        if (band is None):
            band = ["bol"]

        for b in band:
            print b
            t,l = self.get_lightcurve(band=b,magnitudes=magnitudes,view=view)

            col = 'k'
            if (b == 'B'): col = 'b'
            if (b == 'V'): col = 'g'


            plt.plot(t,l,lw=3,color=col)

        if (xrange is not None):
            plt.xlim(xrange)
        if (yrange is not None):
            plt.ylim(yrange)

        if (logx is not None):
            plt.xscale("log")
        if (logy is not None):
            plt.yscale("log")
        if (logxy is not None):
            plt.xscale("log")
            plt.yscale("log")


        plt.xlabel('time (' + self.time_units + ')',size=12 )
        if (magnitudes is None):
            ylab = "luminosity (erg/s)"
        else:
            ylab = magnitudes + " magnitude"
            plt.gca().invert_yaxis()
        plt.ylabel(ylab,size=12)


        plt.legend(band)
        plt.show()


    def write_lightcurve(self,band=None,outfile=None,magnitudes=None,view=None,format=None):
        """
        Function to write out lightcurve data to a file

        Parameters
        --------------

        """
        if (band is None):
            band = ["bol"]

        if (format is None):
            format = "ascii"

        lc_list = []
        for b in band:
            t,lc = self.get_lightcurve(band=b,magnitudes=magnitudes,view=view)
            lc_list.append(lc)

        nt = len(t)
        nb = len(band)

        if (format == "ascii"):
            fout = open(outfile,'w')

            # write header
            fout.write("#{0:^11}  ".format('t (' + self.time_units + ')'))
            for b in band:
                fout.write("{0:^11}  ".format(b))
            fout.write("\n")
            if (magnitudes is not None):
                fout.write("# magnitudes in the " + magnitudes + " system\n")

            for i in range(nt):
                fout.write("{0:11.4e}  ".format(t[i]))
                for j in range(nb):
                    fout.write("{0:11.4e}  ".format(lc_list[j][i]))
                fout.write("\n")
            fout.close()


###############
## OLDER stuff
##############


    def get_band_lc(self,wrange,view=None,angle_average=False,magnitudes=False):

        Lband = np.zeros([self.n_times,self.n_mu,self.n_phi],dtype='d')

        b = (self.x >= wrange[0])*(self.x <= wrange[1])
        if (sum(b) == 0):
            message = "Wavelength range for band light curve not in spectrum range"
            raise ValueError(message)

        # integrate light curve
        for it,im,ip in np.ndindex(self.n_times,self.n_mu,self.n_phi):
            Lband[it,im,ip] = np.trapz(self.L[it,b,im,ip],x=self.x[b])

        Lband = Lband/(wrange[1] - wrange[0])

        if (self.n_mu == 1 and self.n_phi == 1):
            return self.t,Lband[:,0,0]
        if (self.n_mu > 1 and self.n_phi == 1):
            return self.t,Lband[:,:,0]
        if (self.n_mu == 1 and self.n_phi > 1):
            return self.t,Lband[:,0,:]
        return self.t,Lband

    def get_bolometric_lc_old(self,view=None,angle_average=False,magnitudes=False):

        # compute bolometric light curve
        if (self.Lbol is None):
            self.Lbol = np.zeros([self.n_times,self.n_mu,self.n_phi],dtype='d')
            self.Lave = np.zeros([self.n_times])

        # integrate bolometric light curve
        for it,im,ip in np.ndindex(self.n_times,self.n_mu,self.n_phi):
            if (self.n_x == 1):
                self.Lbol[it,im,ip] = self.L[it,0,im,ip]
                self.Lave[it] += self.L[it,0,im,ip]
            else:
                self.Lbol[it,im,ip] = np.trapz(self.L[it,:,im,ip],x=self.x)
                self.Lave[it] += self.Lbol[it,im,ip]

                self.Lave = self.Lave/(1.0*self.n_mu*self.n_phi)

        Lbol = self.Lbol
        Lave = self.Lave

        thisL = self.Lbol
        if (angle_average):
            thisL = self.Lave

        if (magnitudes):
            thisL = -2.5*np.log10(thisL)+88.697425

        if (self.n_mu == 1 and self.n_phi == 1):
            return self.t,Lbol #thisL[:,0,0]
        if (self.n_mu > 1 and self.n_phi == 1):
            return self.t,thisL[:,:,0]

        # interpolate to particular viewing angle
        if (view is not None):
            mu  = view[0]
            phi = view[1]
            import bisect

            imu = bisect.bisect_left(self.mu,mu)
            i1 = imu-1
            i2 = imu
            if (i2 == len(self.mu)): i2 = imu-1
            m1 = self.mu[i1]
            m2 = self.mu[i2]
            dm = mu - m1
    #        print i1,i2,m1,m2,mu


            jphi = bisect.bisect_left(self.phi,phi)
            nphi = len(self.phi)
            if (jphi == 0):
                j1 = nphi-1
                j2 = 0
                p1 = self.phi[j1] - 2.0*np.pi
                p2 = self.phi[j2]
            elif (jphi == nphi):
                j1 = nphi-1
                j2 = 0
                p1 = self.phi[j1]
                p2 = self.phi[j2] + 2.0*np.pi
            else:
                j1 = jphi -1
                j2 = jphi
                p1 = self.phi[j1]
                p2 = self.phi[j2]

            print p1,phi,p2, jphi,j1,j2

            p1 = self.phi[j1]
            p2 = self.phi[j2]
            if (jphi == len(self.phi)):
                j2 = jphi - 1
                j1 = 0
                p1 = np.pi*2.0 + self.phi[0]


            dp = phi - p1

            Llo = self.Lbol[:,i1,j1]
            Lhi = self.Lbol[:,i2,j1]
            if (m2 == m1):
                L1 = Llo
            else:
                L1  =   Llo + (Lhi - Llo)*dm/(m2 - m1)

            Llo = self.Lbol[:,i1,j2]
            Lhi = self.Lbol[:,i2,j2]
            if (m2 == m1):
                L2 = Llo
            else:
                L2  = Llo + (Lhi - Llo)*dm/(m2 - m1)

            if (p2 == p1):
                Lint = L1
            else:
                Lint = L1 + (L2 - L1)*dp/(p2 - p1)

            thisL = Lint


        return self.t,thisL




    def get_spectrum(self,time=None,mu=None,phi=None,interpolate=True,angle_average=False):

        import bisect

        nt = len(self.t)
        nmu  = len(self.mu)
        nphi = len(self.phi)
        nx   = len(self.x)

        # for just a snapshot 1D spectrum
        if (nt == 1 and nmu == 1 and nphi == 1):
            return self.x, self.L[0,:,0,0]

        smp = np.zeros((nx,nmu,nphi))

        if (interpolate and nt > 1):

            # interpolate in time
            indt = bisect.bisect_left(self.t,time)
            i1 = indt-1
            i2 = indt
            t1 = self.t[i1]
            t2 = self.t[i2]
            dt = time - t1
            for i in range(nmu):
                for j in range (nphi):
                    s1 = self.L[i1,:,i,j]
                    s2 = self.L[i2,:,i,j]
                    smp[:,i,j] = s1 + (s2 - s1)*dt/(t2 - t1)

        else:
            if (nt == 1):
                smp = self.L[0,:,:,:]
            else:
                indt = bisect.bisect(self.t,time)
                smp = self.L[indt,:,:,:]

            # interpolate in mu, if wanted
#            if (mu is not None):
#                imu = bisect.bisect_left(self.mu,mu)
#                i1 = imu-1
#                i2 = imu
#                m1 = self.mu[i1]
#                m2 = self.mu[i2]
#                dm = mu - m1
#                for j in range (nphi):
#                    s1 = smp[:,i1,j]
#                    s2 = smp[:,i2,j]
#                    sm[:,j] = s1 + (s2 - s1)*dm/(m2 - m1)

        if (angle_average):
            Fave = np.zeros(self.n_x)
            for im,ip in np.ndindex(self.n_mu,self.n_phi):
                Fave += smp[:,im,ip]
            Fave /= 1.0/(1.0*self.n_mu*self.n_phi)
            return self.x, Fave

        if (nmu == 1 and nphi == 1):
            return self.x,smp[:,0,0]
        elif (nmu == 1):
            return self.x,smp[:,0,:]
        else:
            return self.x,smp[:,:,:]


    def get_nuLnu_lc(self,wrange,magnitudes=False):
        b = (self.x >= wrange[0])*(self.x <= wrange[1])
        new_x = self.x[b]
        lc = np.zeros(self.t.size)
        for i in range(self.t.size):
            lc[i] = np.trapz(self.L[i,b,0,0],x=new_x)


        if (magnitudes):
            lc = lc/(wrange[1] - wrange[0])

        else:
            lc = lc*0.5*(wrange[0] + wrange[1])/(wrange[1] - wrange[0])

        return self.t,lc


    def get_ABMag(self,band):
        """
        returns the AB magnitude light curve, for a given band
        """
        lum = np.zeros(self.t.size)
        dnu = np.ediff1d(self.nu,to_end=0)
        for i in range(self.time.size):
            sample_points = self.spec_tnu[i, :]/self.nu*(self.filter.transFunc_nu(band)(self.nu))
            lum[i] = inte.trapz(sample_points,x=self.nu)
            lum = lum/self.filter.getNormalization(band,self.nu) #gets Lnu(band)
            flx = lum/(4.*np.pi*(10.*3.0857e18)**2) #convert to flux at 10pc
            flx[np.where(flx==0.)] 	= 1e-99 #some small number so not taking log(0)
            mag = -2.5*np.log10(flx)-48.6 #get the AB magnitude
            mag[np.where(mag>0)] = 0. #set minimum mag to 0
            return self.t,mag
