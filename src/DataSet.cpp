// DataSet
#include <cstring>
#include "DataSet.h"
#include "CpptrajStdio.h"

// CONSTRUCTOR
DataSet::DataSet() {
  //fprintf(stderr,"DataSet Constructor.\n");
  name=NULL;
  idx=-1;
  N=0;
  isDynamic=false;
  current=0;
  width = 0;
  precision = 0;
  format = NULL;
  dType = UNKNOWN_DATA;
}

// DESTRUCTOR
DataSet::~DataSet() {
  //fprintf(stderr,"DataSet Destructor\n");
  if (name!=NULL) delete[] name;
  if (format!=NULL) delete[] format;
}

// DataSet::setFormatString()
/** Set up the output format string for each data element based on the given 
  * dataType and the current width, and precision.
  */
// NOTE: Should this be split up and moved into the individual datasets?
void DataSet::setFormatString() {
  if (format!=NULL) {delete[] format; format=NULL;}

  switch (dType) {
    case DOUBLE :
    case FLOAT  :
      format = SetDoubleFormatString(width, precision);
      break;
    case STRING :
      format = SetStringFormatString(width);
      break;
    case INT    :
      format = SetIntegerFormatString(width);
      break;
    case XYZ :
      format = SetXYZFormatString(width,precision);
      break;
    case UNKNOWN_DATA :
      mprintf("Internal Error: setFormatString called with unknown data type.\n");
  }

  if (format==NULL) 
    mprintf("Error: setFormatString: Could not allocate memory for string.\n");
  // DEBUG
  //else
  //  mprintf("DEBUG: Format string: [%s]\n",format);
}    

// DataSet::SetPrecision()
/** Set dataset width and precision and recalc output format string.
  */
void DataSet::SetPrecision(int widthIn, int precisionIn) {
  width=widthIn;
  precision=precisionIn;
  setFormatString();
}

// DataSet::Setup()
/** Set up common to all data sets. The dataset name should be unique and is
  * checked for in DataSetList prior to this call. Nin is the expected size 
  * of the dataset. If Nin<=0 the dataset will be allocated dynamically.
  */
int DataSet::Setup(char *nameIn, int Nin) {
  // Dataset name
  if (nameIn==NULL) {
    mprintf("Dataset has no name.\n");
    return 1;
  }
  name = new char[ strlen(nameIn)+1 ]; 
  strcpy(name, nameIn);
  // Dataset memory
  N=Nin;
  if (N<=0) {
    isDynamic=true;
    N=0;
  }
  if ( this->Allocate() ) return 1;
  return 0;
}

// DataSet::Info()
void DataSet::Info() {
  mprintf("    Data set %s",name);
  mprintf(", size is ");
  if (isDynamic)
    mprintf("dynamic");
  else
    mprintf("%i",N);
  mprintf(", current is %i",current);
  mprintf(".\n");
}

// DataSet::WriteNameToBuffer()
/** Write the dataset name to the given character buffer.
  */
void DataSet::WriteNameToBuffer(CharBuffer &cbuffer, bool leftAlign) {
  cbuffer.WriteStringN(name,width,leftAlign);
}

// DataSet::CheckSet()
/** Return 1 if current==0, which indicates set has not been written to.
  * Otherwise return 0.
  * Call setFormatString; mostly just needed for string data sets which
  * have variable width based on the size of the strings that have been
  * stored.
  */
int DataSet::CheckSet() {
  if (current==0) return 1;
  setFormatString();
  //mprinterr("Dataset %s has format [%s]\n",name,format);
  return 0;
}

