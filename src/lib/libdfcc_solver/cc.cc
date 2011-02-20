#include "cc.h"

#include <libqt/qt.h>
#include <libchkpt/chkpt.hpp>
#include <libmints/mints.h>

using namespace std;
using namespace psi;

namespace psi { namespace dfcc {

CC::CC(Options& options, shared_ptr<PSIO> psio, shared_ptr<Chkpt> chkpt)
  : Wavefunction(options, psio, chkpt)
{
  get_params();
  get_ribasis();
//get_dealiasbasis(); // We don't always want this
}

CC::~CC()
{
}

void CC::get_options()
{
}

void CC::get_params()
{
  // Init with checkpoint until Rob gets bitchy
  // RP: I'm bitchy. But not a bitch unless you want chinese food.
  // MO basis info
  nirrep_ = chkpt_->rd_nirreps();

  if (nirrep_ != 1)
    throw PsiException("You want symmetry? Try ccenergy", __FILE__,
      __LINE__);

  int *clsdpi_ = new int[8];
  int *orbspi_ = new int[8];
  int *frzcpi_ = new int[8];
  int *frzvpi_ = new int[8];

  clsdpi_ = chkpt_->rd_clsdpi();
  orbspi_ = chkpt_->rd_orbspi();
  frzcpi_ = chkpt_->rd_frzcpi();
  frzvpi_ = chkpt_->rd_frzvpi();

  nso_ = chkpt_->rd_nso();
  nmo_ = orbspi_[0];
  nocc_ = clsdpi_[0];
  nvir_ = orbspi_[0]-clsdpi_[0];
  nfocc_ = frzcpi_[0];
  nfvir_ = frzvpi_[0];
  naocc_ = clsdpi_[0]-frzcpi_[0];
  navir_ = orbspi_[0]-clsdpi_[0]-frzvpi_[0];
  namo_ = navir_ + naocc_;

  delete[] clsdpi_;
  delete[] orbspi_;
  delete[] frzcpi_;
  delete[] frzvpi_;

  // Reference wavefunction info
  Eref_ = chkpt_->rd_escf();
  double* evals_t = chkpt_->rd_evals();
  double** C_t = chkpt_->rd_scf();

  evals_ = shared_ptr<Vector>(new Vector("Epsilon (full)",nmo_));
  evalsp_ = evals_->pointer();
  memcpy(static_cast<void*> (evalsp_), static_cast<void*> (evals_t), nmo_*sizeof(double));
  C_ = shared_ptr<Matrix>(new Matrix("C (full)", nso_, nmo_));
  Cp_ = C_->pointer();
  memcpy(static_cast<void*> (Cp_[0]), static_cast<void*> (C_t[0]), nmo_*nso_*sizeof(double));

  // Convenience matrices (may make it easier on the helper objects)
  // ...because Rob is a pussy
  // asshole
  evals_aocc_ = shared_ptr<Vector>(new Vector("Epsilon (Active Occupied)",naocc_));
  evals_aoccp_ = evals_aocc_->pointer();
  evals_avir_ = shared_ptr<Vector>(new Vector("Epsilon (Active Virtual)",navir_));
  evals_avirp_ = evals_avir_->pointer();

  C_aocc_ = shared_ptr<Matrix>(new Matrix("C (Active Occupied)", nso_, naocc_));
  C_aoccp_ = C_aocc_->pointer();
  C_avir_ = shared_ptr<Matrix>(new Matrix("C (Active Virtual)", nso_, navir_));
  C_avirp_ = C_avir_->pointer();

  memcpy(static_cast<void*> (evals_aoccp_), static_cast<void*> (&evals_t[nfocc_]), naocc_*sizeof(double));
  memcpy(static_cast<void*> (evals_avirp_), static_cast<void*> (&evals_t[nocc_]), navir_*sizeof(double));

  for (int m = 0; m < nso_; m++) {
    memcpy(static_cast<void*> (C_aoccp_[m]), static_cast<void*> (&C_t[m][nfocc_]), naocc_*sizeof(double));
    memcpy(static_cast<void*> (C_avirp_[m]), static_cast<void*> (&C_t[m][nocc_]), naocc_*sizeof(double));
  }

  free(evals_t);
  free_block(C_t);
}

void CC::get_ribasis()
{
  shared_ptr<BasisSetParser> parser(new Gaussian94BasisSetParser());
  ribasis_ = BasisSet::construct(parser, molecule_, "RI_BASIS_CC");
  zero_ = BasisSet::zero_ao_basis_set();
  ndf_ = ribasis_->nbf();
}
void CC::get_dealiasbasis()
{
  shared_ptr<BasisSetParser> parser(new Gaussian94BasisSetParser());
  if (options_.get_str("DEALIAS_BASIS_CC") != "") {
    dealias_ = BasisSet::construct(parser, molecule_, "DEALIAS_BASIS_CC"); 
    ndealias_ = dealias_->nbf();
  } else {
    ndealias_ = 0;
  } 
}

void CC::print_header()
{
     fprintf(outfile,"    Orbital Information\n");
     fprintf(outfile,"  -----------------------\n");
     fprintf(outfile,"    NSO      = %8d\n",nso_);
     fprintf(outfile,"    NMO Tot  = %8d\n",nmo_);
     fprintf(outfile,"    NMO Act  = %8d\n",namo_);
     fprintf(outfile,"    NOCC Tot = %8d\n",nocc_);
     fprintf(outfile,"    NOCC Frz = %8d\n",nfocc_);
     fprintf(outfile,"    NOCC Act = %8d\n",naocc_);
     fprintf(outfile,"    NVIR Tot = %8d\n",nvir_);
     fprintf(outfile,"    NVIR Frz = %8d\n",nfvir_);
     fprintf(outfile,"    NVIR Act = %8d\n",navir_);
     fprintf(outfile,"    NDF      = %8d\n",ndf_);
     fprintf(outfile,"\n");
     fflush(outfile);
}

void CC::iajb_ibja(double *ijkl)
{
  double *X = init_array(navir_);

  for (int i=0; i<naocc_; i++) {
  for (int a=0; a<navir_; a++) {
    for (int j=0; j<naocc_; j++) {
    int ia = i*navir_ + a;
    int ja = j*navir_ + a;
    long int iajb = ia*naocc_*(long int) navir_ + (long int) j*navir_;
    long int ibja = i*navir_*naocc_*(long int) navir_ + (long int) ja;
    C_DCOPY(navir_,&(ijkl[iajb]),1,X,1);
    C_DCOPY(navir_,&(ijkl[ibja]),naocc_*navir_,&(ijkl[iajb]),1);
    C_DCOPY(navir_,X,1,&(ijkl[ibja]),naocc_*navir_);
  }}}

  free(X);
}

void CC::iajb_ijab(double *ijkl)
{
  double **X = block_matrix(navir_,naocc_);

  for (int i=0; i<naocc_; i++) {
  for (int b=0; b<navir_; b++) {
    long int iajb = i*navir_*naocc_*(long int) navir_ + (long int) b;
    C_DCOPY(naocc_*navir_,&(ijkl[iajb]),navir_,X[0],1);
    for (int j=0; j<naocc_; j++) {
      int ij = i*naocc_ + j;
      long int ijab = ij*navir_*(long int) navir_ + (long int) b;
      C_DCOPY(navir_,&(ijkl[ijab]),navir_,&(X[0][j]),naocc_);
    }
  }}

  free_block(X);
}

DFCCDIIS::DFCCDIIS(int diisfile, int ampfile, char *amplabel, char *errlabel, 
  int length, int maxvec, shared_ptr<PSIO> psio) : psio_(psio)
{   
    diis_file_ = diisfile;
    psio_->open(diis_file_,0);
    
    max_diis_vecs_ = maxvec;
    
    filenum_ = ampfile;
    vec_label_ = amplabel;
    err_label_ = errlabel;
    
    vec_length_ = length;

    curr_vec_ = 0;
    num_vecs_ = 0;
}   
    
DFCCDIIS::~DFCCDIIS()
{   
    psio_->close(diis_file_,0);
}

void DFCCDIIS::store_vectors()
{
    char *diis_vec_label = get_vec_label(curr_vec_);
    char *diis_err_label = get_err_label(curr_vec_);
    curr_vec_ = (curr_vec_+1)%max_diis_vecs_;
    num_vecs_++;
    if (num_vecs_ > max_diis_vecs_) num_vecs_ = max_diis_vecs_;
  
    double *vec = init_array(vec_length_);

    psio_->read_entry(filenum_,vec_label_,(char *) &(vec[0]),
      vec_length_*(ULI) sizeof(double));
    psio_->write_entry(diis_file_,diis_vec_label,(char *) &(vec[0]),
      vec_length_*(ULI) sizeof(double));

    psio_->read_entry(filenum_,err_label_,(char *) &(vec[0]),
      vec_length_*(ULI) sizeof(double));
    psio_->write_entry(diis_file_,diis_err_label,(char *) &(vec[0]),
      vec_length_*(ULI) sizeof(double));

    free(vec);
    free(diis_vec_label);
    free(diis_err_label);
}

void DFCCDIIS::get_new_vector()
{
    int *ipiv;
    double *Cvec;
    double **Bmat;

    ipiv = init_int_array(num_vecs_+1);
    Bmat = block_matrix(num_vecs_+1,num_vecs_+1);
    Cvec = (double *) malloc((num_vecs_+1)*sizeof(double));

    double *vec_i = init_array(vec_length_);
    double *vec_j = init_array(vec_length_);

    for (int i=0; i<num_vecs_; i++) {
      char *err_label_i = get_err_label(i);
      psio_->read_entry(diis_file_,err_label_i,(char *) &(vec_i[0]),
        vec_length_*(ULI) sizeof(double));
      for (int j=0; j<=i; j++) {
        char *err_label_j = get_err_label(j);
        psio_->read_entry(diis_file_,err_label_j,(char *) &(vec_j[0]),
          vec_length_*(ULI) sizeof(double));
        Bmat[i][j] = Bmat[j][i] = C_DDOT(vec_length_,vec_i,1,vec_j,1);
        free(err_label_j);
      }
      free(err_label_i);
    }

    for (int i=0; i<num_vecs_; i++) {
      Bmat[num_vecs_][i] = -1.0;
      Bmat[i][num_vecs_] = -1.0;
      Cvec[i] = 0.0;
    }

    Bmat[num_vecs_][num_vecs_] = 0.0;
    Cvec[num_vecs_] = -1.0;

    C_DGESV(num_vecs_+1,1,&(Bmat[0][0]),num_vecs_+1,&(ipiv[0]),&(Cvec[0]),
      num_vecs_+1);

    memset(vec_j,'\0',sizeof(double)*vec_length_);

    for (int i=0; i<num_vecs_; i++) {
      char *vec_label_i = get_vec_label(i);
      psio_->read_entry(diis_file_,vec_label_i,(char *) &(vec_i[0]),
        vec_length_*(ULI) sizeof(double));
      C_DAXPY(vec_length_,Cvec[i],vec_i,1,vec_j,1);
      free(vec_label_i);
    }

    psio_->write_entry(filenum_,vec_label_,(char *) &(vec_j[0]),
      vec_length_*(ULI) sizeof(double));

    free(vec_i);
    free(vec_j);

    free(ipiv);
    free(Cvec);
    free_block(Bmat);
}

char *DFCCDIIS::get_err_label(int num)
{
    char *label = (char *) malloc(16*sizeof(char));
    sprintf(label,"Error vector %2d",num);
    return(label);
}

char *DFCCDIIS::get_vec_label(int num)
{
    char *label = (char *) malloc(10*sizeof(char));
    sprintf(label,"Vector %2d",num);
    return(label);
}

}}