import os
import matplotlib.pyplot as plt
import numpy as np
import h5py
import sys


def run_test(pdf="",runcommand=""):

    ###########################################
    # clean up old results and run the code
    ###########################################
    if (runcommand != ""):
    	os.system("rm *_spectrum_* plt_* integrated_quantities.dat")
    	os.system(runcommand)

    ###########################################
    # compare the output
    ###########################################
    plt.clf()
    failure = 0

    # sedona results
    fin = h5py.File('optical_spectrum_final.h5','r')
    tlc = np.array(fin['time'])
    Lnu = np.array(fin['Lnu'])
    mu  = np.array(fin['mu'])
    phi = np.array(fin['phi'])

    tlc = tlc/3600.0/24.0

    # get and plot angle integrated light curve
    total_lc = np.zeros(len(tlc))
    for i in range(len(mu)):
        for j in range(len(phi)):
            total_lc += Lnu[:,0,i,j]
    total_lc = total_lc/(1.0*len(mu)*len(phi))
    plt.plot(tlc,total_lc,'o',markeredgecolor='k',markersize=6,markeredgewidth=2)

    # plot radioactive deposition
    ts2,erad,Ls2,Lnuc = np.loadtxt('integrated_quantities.dat',usecols=[0,1,2,3],unpack=1,skiprows=1)
    ts2= ts2/3600.0/24.0
    plt.plot(ts2,Ls2,'o',markeredgecolor='blue',markersize=8,markeredgewidth=2,markerfacecolor='none')

    # plot benchmark results
    tl1,Ll1 = np.loadtxt('../comparefiles/lucy_lc.dat',unpack=1)
    plt.plot(tl1,Ll1,color='k',linewidth=3)
    tl2,Ll2 = np.loadtxt('../comparefiles/lucy_gr.dat',unpack=1)
    plt.plot(tl2,Ll2,color='blue',linewidth=3)
    plt.ylim(1e40,0.4e44)

    # overplot angle dependent light curves
    for i in range(len(mu)):
        for j in range(len(phi)):
#        plt.plot(tlc,Lnu[:,0,i],'o',markeredgecolor='red',markersize=6,markeredgewidth=2,markerfacecolor='none',alpha=0.2)
            plt.plot(tlc,Lnu[:,0,i,j],color='r',alpha=0.2,linewidth=0.5)
            # calculate error
            use = ((tlc > 3)*(tlc < 55))
            max_err,mean_err = get_error(Lnu[:,0,i,j],Ll1,x=tlc,x_comp=tl1,use = use)
#        if (max_err > 0.4): failure = 1
            if (mean_err > 0.15): failure = 1
#            print (mean_err,max_err)

    use = ((ts2 > 3)*(ts2 < 55))
    max_err,mean_err = get_error(Ls2,Ll2,x=ts2,x_comp=tl2,use = use)
    if (max_err > 0.25): failure = 2
    if (mean_err > 0.1): failure = 2

    ## make plot
    plt.title('Lucy Supernova Test -- 3D Cart, testing nonuniform grid')
    plt.legend(['sedona LC','sedona GR','lucy LC','lucy GR'])
    plt.xlim(0,55)
#    plt.yscale('log')
    plt.xlabel('luminosity (erg/s)',size=13)
    plt.ylabel('days since explosion',size=13)
    if (pdf != ''): pdf.savefig()
    else:
        plt.ion()
        plt.show()
        j = get_input('press any key> ')

    return failure


#-------------------------------------------
# error calculator helper function
#-------------------------------------------

def get_error(a,b,x=[],x_comp=[],use=[]):

    """ Function to calculate the error between two arrays

        Args:
        a: numpy array of result
        b: numpy array of comparison
        use: an array of 0's and 1's telling which element
             in the arrays to include
        x: optional array of x values to go along with a
        x_comp: optional array of x values to go along with b
        (if x and x_comp are set, will interpolate b values to x spacing)

        Returns:
            returns max_error, mean_error in percentages

        Example:
            say you have an array y that is a function of x
            you wnat to see how much it deviates from a reference array y_comp
            but only for values where x > 0.5. Use

            max_error, mean_error = get_error(y,y_comp,use=(x > 0.5))

    """

    # result array
    y = a
    # compare array
    y_comp = b

    # interpolate comparison if wanted
    if (len(x) != 0 and len(x_comp !=0)):
        y_comp = np.interp(x,x_comp,y_comp)

    # cut the array length if wanted
    if (len(use) > 0):
        y = y[use]
        y_comp = y_comp[use]
    err = abs(y - y_comp)

    max_err = max(err/y_comp)
    mean_err = np.mean(err)/np.mean(y_comp)

    return max_err,mean_err

#-----------------------------------------
# little function to just plot up and
# compare results. Assumes code has
# already been run and output files
# are present
#----------------------------------------
if __name__=='__main__':

    # Default to Python 3's input()
    get_input = input
    # If this is Python 2, use raw_input()
    if sys.version_info[:2] <= (2, 7):
        get_input = raw_input

    status = run_test('')
    if (status == 0):
        print ('SUCCESS')
    else:
        print ('FAILURE, code = ' + str(status))
