#include <cmath> //sqrt
#include "Action_CheckFrame.h"
#include "CpptrajStdio.h"
#ifdef _OPENMP
#  include "omp.h"
#endif

// CONSTRUCTOR
Action_CheckFrame::Action_CheckFrame() :
  bondoffset_(1.15),
  nonbondcut2_(0.64), // 0.8^2
  outfile_(0),
  CurrentParm_(0),
  debug_(0),
  silent_(false),
  skipBadFrames_(false),
  bondcheck_(true)
{}

void Action_CheckFrame::Help() {
  mprintf("\t[<mask>] [around <mask2>] [reportfile <report>] [noimage] [skipbadframes]\n"
          "\t[offset <offset>] [cut <cut>] [nobondcheck] [silent]\n"
          "  Check frames for atomic overlaps and unusual bond lengths\n");
}

// Action_CheckFrame::Init()
Action::RetType Action_CheckFrame::Init(ArgList& actionArgs, TopologyList* PFL, DataSetList* DSL, DataFileList* DFL, int debugIn)
{
  debug_ = debugIn;
  // Get Keywords
  image_.InitImaging( !(actionArgs.hasKey("noimage")) );
  std::string around = actionArgs.GetStringKey("around");
  std::string reportFile = actionArgs.GetStringKey("reportfile");
  bondoffset_ = actionArgs.getKeyDouble("offset",1.15);
  double nonbondcut = actionArgs.getKeyDouble("cut",0.8);
  bondcheck_ = !actionArgs.hasKey("nobondcheck");
  skipBadFrames_ = actionArgs.hasKey("skipbadframes");
  silent_ = actionArgs.hasKey("silent");
  if (!silent_) outfile_ = DFL->AddCpptrajFile(reportFile, "Structure check",
                                               DataFileList::TEXT, true);
  // Get Masks
  Mask1_.SetMaskString( actionArgs.GetMaskNext() );
  if (!around.empty())
    Mask2_.SetMaskString( around );

  mprintf("    CHECKSTRUCTURE: Checking atoms in mask '%s'",Mask1_.MaskString());
  if (Mask2_.MaskStringSet())
    mprintf(" around mask '%s'", Mask2_.MaskString());
  if (!image_.UseImage())
    mprintf(", imaging off");
  if (outfile_ != 0)
    mprintf(", output to %s", outfile_->Filename().full());
  mprintf(".\n");
  if (!bondcheck_) {
    mprintf("\tChecking inter-atomic distances only.\n");
    mprintf("\tWarnings will be printed for non-bond distances < %.2f Ang.\n", nonbondcut);
  } else {
    mprintf("\tChecking inter-atomic and bond distances.\n");
    mprintf("\tWarnings will be printed for bond lengths > eq + %.2f Ang\n",
            bondoffset_);
    mprintf("\tand non-bond distances < %.2f Ang.\n",nonbondcut);
  }
  if (skipBadFrames_)
    mprintf("\tFrames with problems will be skipped.\n");
  if (silent_)
    mprintf("\tWarning messages will be suppressed.\n");
# ifdef _OPENMP
# pragma omp parallel
  {
#   pragma omp master
    {
    mprintf("\tParallelizing calculation with %i threads.\n", omp_get_num_threads());
    }
  }
# endif
  // Square the non-bond cutoff
  nonbondcut2_ = nonbondcut * nonbondcut;
  return Action::OK;
}

/** Set up bond arrays in a sorted list for easy access during loop
  * over all pairs of atoms. Only use bonds for which both atoms are in
  * the mask.
  */
void Action_CheckFrame::ProcessBondArray(BondArray const& Bonds, BondParmArray const& Parm,
                                         AtomMask const& cMask)
{
  BondType BT;
  for (BondArray::const_iterator bnd = Bonds.begin(); bnd != Bonds.end(); ++bnd)
  {
    if ( cMask.AtomInCharMask(bnd->A1()) && cMask.AtomInCharMask(bnd->A2()) ) {
      if (bnd->Idx() < 0)
        mprintf("Warning: Bond parameters not present for atoms %i-%i, skipping.\n",
                bnd->A1()+1, bnd->A2()+1);
      else {
        BT.Req_off2_ = Parm[ bnd->Idx() ].Req() + bondoffset_;
        BT.Req_off2_ *= BT.Req_off2_; // Store squared values.
        BT.a1_ = bnd->A1();
        BT.a2_ = bnd->A2();
        bondList_.push_back(BT);
      }
    }
  }
}

/** Set up bond parameters for bonds for which both atoms present in mask. */
void Action_CheckFrame::SetupBondList(AtomMask const& iMask, Topology const& top) {
  AtomMask cMask = iMask;
  cMask.ConvertToCharMask();
 
  ProcessBondArray(top.Bonds(),  top.BondParm(), cMask);
  ProcessBondArray(top.BondsH(), top.BondParm(), cMask);
}

// Action_CheckFrame::Setup()
Action::RetType Action_CheckFrame::Setup(Topology* currentParm, Topology** parmAddress) {
  CurrentParm_ = currentParm;
  image_.SetupImaging( currentParm->BoxType() );
  bondList_.clear();
  // Set up masks
  if ( currentParm->SetupIntegerMask( Mask1_ ) ) return Action::ERR;
  Mask1_.MaskInfo();
  if (Mask1_.None()) {
    mprinterr("Error: Mask '%s' has no atoms.\n", Mask1_.MaskString());
    return Action::ERR;
  }
  if (bondcheck_) SetupBondList(Mask1_, *currentParm);
  if ( Mask2_.MaskStringSet() ) {
    if (currentParm->SetupIntegerMask( Mask2_ ) ) return Action::ERR;
    Mask2_.MaskInfo();
    if (Mask2_.None()) {
      mprinterr("Error: Mask '%s' has no atoms.\n", Mask2_.MaskString());
      return Action::ERR;
    }
    // Outer mask should be the one with the most atoms.
    if ( Mask2_.Nselected() > Mask1_.Nselected() ) {
      OuterMask_ = Mask2_;
      InnerMask_ = Mask1_;
    } else {
      OuterMask_ = Mask1_;
      InnerMask_ = Mask2_;
    }
    if (bondcheck_) SetupBondList(Mask2_, *currentParm);
  }
  // Print imaging info for this parm
  if (bondcheck_)
    mprintf("\tChecking %u bonds.\n", bondList_.size());
  if (image_.ImagingEnabled())
    mprintf("\tImaging on.\n");
  else
    mprintf("\timaging off.\n");
  return Action::OK;
}

/** Check for bad bond lengths. */
int Action_CheckFrame::CheckBonds(int frameNum, Frame const& currentFrame, Topology const& top)
{
  double D2;
  int idx;
  int Nproblems = 0;

  int bond_max = (int)bondList_.size();
# ifdef _OPENMP
# pragma omp parallel private(idx,D2) reduction(+: Nproblems)
  {
  //mprintf("OPENMP: %i threads\n",omp_get_num_threads());
  //mythread = omp_get_thread_num();
# pragma omp for
# endif
  for (idx = 0; idx < bond_max; idx++) {
    D2 = DIST2_NoImage( currentFrame.XYZ(bondList_[idx].a1_),
                        currentFrame.XYZ(bondList_[idx].a2_) );
    if (D2 > bondList_[idx].Req_off2_) {
      ++Nproblems;
      if (outfile_ != 0) {
#       ifdef _OPENMP
#       pragma omp critical
#       endif
        outfile_->Printf(
                    "%i\t Warning: Unusual bond length %i:%s to %i:%s (%.2lf)\n", frameNum,
                    bondList_[idx].a1_+1, top.TruncResAtomName(bondList_[idx].a1_).c_str(),
                    bondList_[idx].a2_+1, top.TruncResAtomName(bondList_[idx].a2_).c_str(),
                    sqrt(D2));
      }
    }
  } // END loop over bonds
# ifdef _OPENMP
  } // END pragma omp parallel
# endif
  return Nproblems;   
}

/** Check for bad overlaps. */
int Action_CheckFrame::CheckOverlap(int frameNum, Frame const& currentFrame, Topology const& top)
{
  double D2;
  Matrix_3x3 ucell, recip; // ToFrac, ToCart
  int nmask1, nmask2;
  int atom1, atom2;
  int Nproblems = 0;

  // Get imaging info for non-orthogonal box // TODO Check volume
  if (image_.ImageType()==NONORTHO)
    currentFrame.BoxCrd().ToRecip(ucell, recip);
  if ( Mask2_.MaskStringSet() ) {
    // Calculation of all atoms in Mask1 to all atoms in Mask2
    int outer_max = OuterMask_.Nselected();
    int inner_max = InnerMask_.Nselected();
#   ifdef _OPENMP
#   pragma omp parallel private(nmask1,nmask2,atom1,atom2,D2) reduction(+: Nproblems)
    {
    //mprintf("OPENMP: %i threads\n",omp_get_num_threads());
    //mythread = omp_get_thread_num();
#   pragma omp for
#   endif
    for (nmask1 = 0; nmask1 < outer_max; nmask1++) {
      atom1 = OuterMask_[nmask1];
      for (nmask2 = 0; nmask2 < inner_max; nmask2++) {
        atom2 = InnerMask_[nmask2];
        if (atom1 != atom2) {
          D2 = DIST2( currentFrame.XYZ(atom1), currentFrame.XYZ(atom2),
                      image_.ImageType(), currentFrame.BoxCrd(), ucell, recip);
          if (D2 < nonbondcut2_) {
            ++Nproblems;
            if (outfile_ != 0) {
#             ifdef _OPENMP
#             pragma omp critical
#             endif
              outfile_->Printf(
                    "%i\t Warning: Atoms %i:%s and %i:%s are close (%.2lf)\n", frameNum,
                    atom1+1, top.TruncResAtomName(atom1).c_str(),
                    atom2+1, top.TruncResAtomName(atom2).c_str(), sqrt(D2));
            }
          }
        }
      } // END loop over inner mask
    } // END loop over outer mask
#   ifdef _OPENMP
    } // END pragma omp parallel
#   endif
  } else {
    // Calculation of atoms in Mask1 to all other atoms in Mask1
    int mask1_max = Mask1_.Nselected();
#   ifdef _OPENMP
#   pragma omp parallel private(nmask1,nmask2,atom1,atom2,D2) reduction(+: Nproblems)
    {
    //mprintf("OPENMP: %i threads\n",omp_get_num_threads());
    //mythread = omp_get_thread_num();
#   pragma omp for schedule(dynamic)
#   endif
    for (nmask1 = 0; nmask1 < mask1_max; nmask1++) {
      atom1 = Mask1_[nmask1];
      for (nmask2 = nmask1 + 1; nmask2 < mask1_max; nmask2++) {
        atom2 = Mask1_[nmask2];
        D2 = DIST2( currentFrame.XYZ(atom1), currentFrame.XYZ(atom2),
                    image_.ImageType(), currentFrame.BoxCrd(), ucell, recip);
        if (D2 < nonbondcut2_) {
          ++Nproblems;
          if (outfile_ != 0) {
#           ifdef _OPENMP
#           pragma omp critical
#           endif
            outfile_->Printf(
                  "%i\t Warning: Atoms %i:%s and %i:%s are close (%.2lf)\n", frameNum,
                  atom1+1, top.TruncResAtomName(atom1).c_str(),
                  atom2+1, top.TruncResAtomName(atom2).c_str(), sqrt(D2));
          }
        }
      } // END inner loop over Mask1
    } // END outer loop over Mask1
#   ifdef _OPENMP
    } // END pragma omp parallel
#   endif
  }

  return Nproblems;
}

// Action_CheckFrame::DoAction()
Action::RetType Action_CheckFrame::DoAction(int frameNum, Frame* currentFrame, Frame** frameAddress)
{
  int total_problems = CheckOverlap(frameNum+1, *currentFrame, *CurrentParm_);
  if (bondcheck_)
    total_problems += CheckBonds(frameNum+1, *currentFrame, *CurrentParm_);

  if (total_problems > 0 && skipBadFrames_)
    return Action::SUPPRESSCOORDOUTPUT;
  return Action::OK;
}