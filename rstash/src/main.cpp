#include "main.hh"
#include "extras.hh"

// Grided climate data is passed from R and accessed via Rcpp wrappers
// ==> cols 01:02 are lat:lon
// ==> cols 03:15 are monthly averages
// Values are then stored in a vector<GridCell>  of length equal to the total number of grid cells
// and contains private members storing the various time-dependent measure of the three drivers.
// GridCell objects are then passed to the evaporation routines where all the relevant bioclimatic
// information is determined.

RcppExport SEXP gridStash( const SEXP R_gtc, const SEXP R_gpr, const SEXP R_gfs, const SEXP R_gcChar ) {

    NumericMatrix   temp(R_gtc), prec(R_gpr), fsun(R_gfs), gcChar(R_gcChar);

    NumericVector   lon = fsun(_,0), lat = fsun(_,1), elev = gcChar(_,2), fcap = gcChar(_,3), swc0 = gcChar(_,4);

    float           miss_val;
    unsigned int    mn;
    unsigned long   ll, ncell = fsun.nrow();

    NumericMatrix   gTOT    (ncell,nvar+2),
                    gAET    (ncell,14), gEET    (ncell,14), gPET    (ncell,14),
                    gDET    (ncell,14), gPAR    (ncell,14), gMI     (ncell,14),
                    gALPHA  (ncell,14), gGDD0   (ncell,14), gGDD5   (ncell,14),
                    gGDD10  (ncell,14), gCHILL  (ncell,14), gRO     (ncell,14),
                    gSWC0   (ncell,3);

    GridCell gridCell;
    // initialise vectors in object
    gridCell.init_Day   (0.0);
    gridCell.init_Month (0.0);
    gridCell.resIn_Year (0.0);

    // create a new line ready for the progress bar
    cout << endl;

    // run through each grid cell and perform daily time-series calculations
    for( ll=0; ll<ncell; ll++ ) {

        // initialise the cell and zero object private members
        gridCell.reset_Day  (0.0);
        gridCell.reset_Month(0.0);
        gridCell.resIn_Year (0.0);

        // set cell characteristics
        gridCell.set_Cell( ll+1 );
        gridCell.set_Elev( elev(ll) );
        gridCell.set_Fcap( fcap(ll) );
        gridCell.set_Coord( lat(ll), lon(ll) );

        // if SWC has a value then set this as the initial condition and no spin-up is required
        if(swc0[ll]!=-9999.) {
            gridCell.init_SMC( swc0[ll] );
            gridCell.set_SpinUp(false);
        } else {
            gridCell.set_SpinUp(true);
        }

        // store monthly climate drivers in the gridCell object (+2 to get rid of lat-lon columns)
        for( mn=0; mn<gridCell.get_MLEN(); mn++ ) {
            gridCell.set_mTEMP( temp(ll,mn+2), mn );
            gridCell.set_mFSUN( fsun(ll,mn+2), mn );
            gridCell.set_mPPT ( prec(ll,mn+2), mn );
        }

        // echo progress to user
        if( ll == 0 ) {
            cout << "STASH Progress:" << endl;
        }
        progress_bar(ll, ncell);

        // if the grid cell does not have any missing values
        miss_val = gridCell.get_Missing();
        if((gridCell.get_mTEMP(1) != miss_val) | (gridCell.get_mFSUN(1) != miss_val) | (gridCell.get_mPPT(1) != miss_val)) {
            // linearly interpolate monthly values to daily values for the climate drivers
            gridCell.linearINT( gridCell, gridCell.get_mFSUN(),  &GridCell::set_dFSUN );
            gridCell.linearINT( gridCell, gridCell.get_mTEMP(),  &GridCell::set_dTEMP );
            gridCell.linearINT( gridCell, gridCell.get_mPPT (),  &GridCell::set_dPPT  );
            // do water balance calculations
            waterBucket( gridCell );
            // perform monthly and annual sums
            gridCell.growDegDay();
            gridCell.monthlySums();
            gridCell.monthlyIndex();
            gridCell.annualSums();
        } else {
            gridCell.set_MissingCell();
        }

        // convert back to matrices for export to R
        assign_Rinit ( gridCell, ll, gSWC0, 364, &GridCell::get_dSMC   );
        assign_Rtotal( gridCell, ll, gTOT );
        assign_Rmonth( gridCell, ll, gAET,     &GridCell::get_mAET     );
        assign_Rmonth( gridCell, ll, gEET,     &GridCell::get_mEET     );
        assign_Rmonth( gridCell, ll, gPET,     &GridCell::get_mPET     );
        assign_Rmonth( gridCell, ll, gDET,     &GridCell::get_mDET     );
        assign_Rmonth( gridCell, ll, gPAR,     &GridCell::get_mPAR     );
        assign_Rmonth( gridCell, ll, gRO,      &GridCell::get_mRUN     );
        assign_Rmonth( gridCell, ll, gMI,      &GridCell::get_mMI      );
        assign_Rmonth( gridCell, ll, gALPHA,   &GridCell::get_mALPHA   );
        assign_Rmonth( gridCell, ll, gGDD0,    &GridCell::get_mGDD0    );
        assign_Rmonth( gridCell, ll, gGDD5,    &GridCell::get_mGDD5    );
        assign_Rmonth( gridCell, ll, gGDD10,   &GridCell::get_mGDD10   );
        assign_Rmonth( gridCell, ll, gCHILL,   &GridCell::get_mCHILL   );

    }
    // flush screen ready for R output
    cout << endl << endl;

    // return SEXP-list to user with outputs
    return List::create(
            _("annual") = gTOT,
            _("act.evap") = gAET,
            _("equ.evap") = gEET,
            _("pot.evap") = gPET,
            _("del.evap") = gDET,
            _("photo.abs") = gPAR,
            _("moist.index") = gMI,
            _("alpha.index") = gALPHA,
            _("run.off") = gRO,
            _("grow.deg0") = gGDD0,
            _("grow.deg5") = gGDD5,
            _("grow.deg10") = gGDD10,
            _("chill.day") = gCHILL,
            _("swc.init") = gSWC0
            );
}

void assign_Rtotal( GridCell &gc, const unsigned long ll, NumericMatrix yGrid ) {
    NumericVector   year_vars(nvar+2);
    year_vars = NumericVector::create(
            gc.get_Lon(),   gc.get_Lat(),
            gc.get_yAET(),  gc.get_yEET(),  gc.get_yPET(),
            gc.get_yDET(),  gc.get_yPAR(),  gc.get_yMI(),
            gc.get_yALPHA(),gc.get_yTEMP(), gc.get_yPPT(),
            gc.get_yFSUN(), gc.get_yRUN(),
            gc.get_yGDD0(),
            gc.get_yGDD5(),
            gc.get_yGDD10(),
            gc.get_yCHILL()
            );
    for( unsigned int i=0; i<nvar+2; i++ ) {
        yGrid(ll,i) = year_vars(i);
    }
}

void assign_Rinit( GridCell &gc, const unsigned long ll, NumericMatrix mGrid, const unsigned int i, float (GridCell::*fget)(const int) const ) {
    mGrid(ll,0) = gc.get_Lon();
    mGrid(ll,1) = gc.get_Lat();
    mGrid(ll,2) = (gc.*fget)(i);
}

void assign_Rmonth( GridCell &gc, const unsigned long ll, NumericMatrix mGrid, float (GridCell::*fget)(const int) const ) {
    mGrid(ll,0) = gc.get_Lon();
    mGrid(ll,1) = gc.get_Lat();
    for( int i=0; i<gc.get_MLEN(); i++ ) {
        mGrid(ll,i+2) = (gc.*fget)(i);
    }
}

void assign_Rmonth( GridCell &gc, const unsigned long ll, NumericMatrix mGrid, int (GridCell::*fget)(const int) const ) {
    mGrid(ll,0) = gc.get_Lon();
    mGrid(ll,1) = gc.get_Lat();
    for( int i=0; i<gc.get_MLEN(); i++ ) {
        mGrid(ll,i+2) = (gc.*fget)(i);
    }
}

