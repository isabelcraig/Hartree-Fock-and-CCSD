
//
//  main.cpp
//  HartreeFock
//
//  Created by Isabel Craig on 1/26/18.
//  Copyright © 2018 Isabel Craig. All rights reserved.
//

#include "HartreeFock.hpp"
#include "Read.hpp"

using namespace std;

HartreeFock::HartreeFock(double tol_dens, double tol_e){

    READIN::val("data/enuc.dat", &enuc);
    READIN::SymMatrix("data/overlap.dat",  &S);
    READIN::SymMatrix("data/kinetic.dat", &T);
    READIN::SymMatrix("data/anuc.dat", &V);
    READIN::TEI("data/eri.dat", &TEI);

    this->tol_dens = tol_dens;
    this->tol_e = tol_e;

    Hcore = T + V;

    SymmetricOrth();              // Symmetric Orthogalization Matrix
    Set_InitialFock();            // Build Initial Guess Fock Matrix
    Set_DensityMatrix();          // Build Initial Density Matrix using occupied MOs
    Set_Energy();                 // Compute the Initial SCF Energy

}

void Diagonlize(Matrix *M, Matrix *evals, Matrix *evecs) {

    Eigen::SelfAdjointEigenSolver<Matrix> solver(*M);
    *evecs = solver.eigenvectors();
    Vector evals_vec = solver.eigenvalues();
    for (int i=0; i<NUM_ORB; i++) {
        for (int j=0; j<NUM_ORB; j++) {
            if (i==j) {
                (*evals)(i,i) = evals_vec(i);
            } else {
                (*evals)(i,j) = 0;
            }
        }
    }
}

void HartreeFock::print_state() {

    cout << "--------------------------------------------------------------------------------" << endl;
    cout << "------------------------ Hartree Fock w/ MP2 Correction ------------------------" << endl;
    cout << "--------------------------------------------------------------------------------" << endl;
    cout << "Nuclear repulsion energy = " << enuc << endl;
    cout << "--------------------------------------------------------------------------------" << endl;
    cout << "Overlap Integrals: \n" << S << endl;
    cout << "--------------------------------------------------------------------------------" << endl;
    cout << "Kinetic-Energy Integrals: \n" << T << endl;
    cout << "--------------------------------------------------------------------------------" << endl;
    cout << "Nuclear Attraction Integrals: \n" << V << endl;
    cout << "--------------------------------------------------------------------------------" << endl;
    cout << "Core Hamiltonian: \n" << Hcore << endl;
    cout << "--------------------------------------------------------------------------------" << endl;
    cout << "Symmetric Orthogalization Matrix: \n" << SOM << endl;
    cout << "--------------------------------------------------------------------------------" << endl;
    cout << "Fock Matrix (In Orthogalized Basis): \n" << F0 << endl;
    cout << "--------------------------------------------------------------------------------" << endl;
    cout << "MO Coefficent Matrix: \n" << C0 << endl;
    cout << "--------------------------------------------------------------------------------" << endl;
    cout << "Density Matrix: \n" << D0 << endl;
    cout << "--------------------------------------------------------------------------------" << endl;
    cout << "Energy: \n" << etot << endl;
}

bool HartreeFock::EConverg(){
    // checks for convergence of the engery value
    delE = (prev_etot - etot);
    return (delE < tol_e);
}

bool HartreeFock::DensConverg(){
    // Checks for convergence of the density matrix
    double val = 0;
    for (int i=0; i<NUM_ORB; i++){
        for (int j=0; j<NUM_ORB; j++){
            val += pow(prev_D0(i,j) - D0(i,j), 2);
        }
    }
    rmsD = pow(val, 0.5);
    return (rmsD < tol_dens);
}

void HartreeFock::CheckEnergy(){

  double expected = -74.991229564312;
  double percent_off = 100 * (EMP2 + etot - expected)/expected;
  cout << "--------------------------------------------------------------------------------" << endl;
  cout << percent_off << " percent off from expected results" << endl;
  cout << "--------------------------------------------------------------------------------" << endl;

}

void HartreeFock::Set_Energy() {
    // Sum over all atomic orbitals
    // of DensityMatrix * (Hcore + Fock)
    eelec = 0;
    for (int i = 0; i < NUM_ORB; i++){
        for (int j = 0; j < NUM_ORB; j++){
            eelec += D0(i,j) * (Hcore(i,j) + F0(i,j));
        }
    }
    etot = eelec + enuc;
}

void HartreeFock::SaveDensity(){
  for (int i = 0; i < NUM_ORB; i++) {
      for (int j = 0; j < NUM_ORB; j++) {
          prev_D0(i,j) = D0(i,j);
      }
  }
}

void HartreeFock::SaveEnergy(){
    prev_etot = etot;
}

void HartreeFock::Iterate(){

    int it = 0;
    cout << "--------------------------------------------------------------------------------" << endl;
    cout << "Iter\t\t" << "Energy\t\t" << endl;
    cout << "--------------------------------------------------------------------------------" << endl;
    while ( !(EConverg() && DensConverg()) ) {
        // Copy to check for convergence

        SaveEnergy();
        SaveDensity();
        Set_Fock();
        Set_DensityMatrix();
        Set_Energy();

        cout << it << "\t\t" << setprecision(6) << etot << endl;

        it ++;
    }
}

void HartreeFock::Set_Fock(){

    int ijkl, ikjl;
    int ij, kl, ik, jl;

    for(int i=0; i < NUM_ORB; i++) {
        for(int j=0; j < NUM_ORB; j++) {

            F0(i,j) = Hcore(i,j);

            for(int k=0; k < NUM_ORB; k++) {
                for(int l=0; l < NUM_ORB; l++) {

                    ij = INDEX(i,j);
                    kl = INDEX(k,l);
                    ijkl = INDEX(ij,kl);
                    ik = INDEX(i,k);
                    jl = INDEX(j,l);
                    ikjl = INDEX(ik,jl);

                    F0(i,j) += D0(k,l) * (2.0 * TEI[ijkl] - TEI[ikjl]);
                }
            }
        }
    }
}

void HartreeFock::SymmetricOrth() {
    // Diagonlizes S, such that S^-1/2 can be easily
    // calculated as L * U^-1/2 * L where L are the
    // evecs and U is the diagonal eigenvalue matrix

    Matrix eval;
    Matrix evec;
    Diagonlize(&S, &eval, &evec);

    for(int i=0; i < NUM_ORB; i++) {
        eval(i,i) = pow(eval(i,i),-0.5);
    }

    SOM = evec * eval * evec.transpose();
}

void HartreeFock::Set_InitialFock(){
    // forms an intial guess fock matrix in orthonormal AO using
    // the core hamiltonian as a Guess, such that
    // Fock = transpose (S^-1/2) * Hcore * S^-1/2
    F0 = SOM.transpose()*Hcore*SOM;
}

void HartreeFock::Set_DensityMatrix(){
    // Builds the density matrix from the occupied MOs
    // By summing over all the occupied spatial MOs

    Diagonlize(&F0, &e0, &C0);      // Diagonlize Fock Matrix
    C0 = SOM*C0;                    // Transform eigenvectors onto original non orthogonal AO basis

    //eig_AO.shed_cols(5,6);
    //D0 = C0*C0.transpose();


    double M;
    for (int i = 0; i < NUM_ORB; i++){
        for (int j = 0; j < NUM_ORB; j++){
            M = 0;
            for(int m=0; m < NUM_OCC; m++) {
                M += C0(i,m) * C0(j,m);
            }
            D0(i,j) = M;
        }
    }
}

void HartreeFock::MOBasisFock() {
    // Tests that the resultant Fock matrix is diagonal in the MO basis
    // orbital elements should be diagonal elements since Fi |xi> = ei |xi>
    // therefore Fij = <xi|F|xj> = ei * dij

    // Convert from AO to MO using LCAO-MO coefficents
    //      MO(i)   = Sum over v of C(m,i) * AO(m)
    // Fij = Sum over m,v of C(m,j) * C(v,i) <psi m|F|psi v>
    //              = Sum over m,v of C(m,j) * C(v,i) * F(m,v)

    setzero(&FMO);
    FMO = C0.transpose() * F0 * C0;
    //cout << "--------------------------------------------------------------------------------" << endl;
    //cout << "MO Basis Fock Matrix:\n" << FMO << endl;

}

void HartreeFock::MP2_Correction(){

    MOBasisFock();
    Set_OrbitalEnergy();
    TEI_Transform_N8();
    EMP2 = MP2_Energy();
    cout << "--------------------------------------------------------------------------------" << endl;
    cout << "MP2 Correction Energy :" << EMP2 << endl;
    cout << "--------------------------------------------------------------------------------" << endl;
    cout << "Corrected Energy :" << EMP2 + etot << endl;
    cout << "--------------------------------------------------------------------------------" << endl;

}

void HartreeFock::Set_OrbitalEnergy(){
    // Diagonal Elements of The Fock Operator
    // in the MO Bais are the orbital Energy values
    for (int i = 0; i< NUM_ORB; i++) {
        E(i) = FMO(i,i);
    }
}

double HartreeFock::MP2_Energy() {

    EMP2 = 0;
    int ia, ja, jb, ib, iajb, ibja;

    for (int i = 0; i< NUM_OCC; i++){
        for (int a = NUM_OCC; a < NUM_ORB; a++){
            ia = INDEX(i,a);
            for (int j = 0; j< NUM_OCC; j++){
                 ja = INDEX(j,a);
                for (int b = NUM_OCC; b < NUM_ORB; b++){
                    jb = INDEX(j,b);
                    ib = INDEX(i,b);
                    iajb = INDEX(ia,jb);
                    ibja = INDEX(ib,ja);
                    EMP2 += TEI_MO(iajb) * ( 2.0 * TEI_MO(iajb) - TEI_MO(ibja) ) / (E(i) + E(j) - E(a) - E(b));
                }
            }
        }
    }
    return EMP2;
}

void HartreeFock::TEI_Transform_N5() {
    /**
    Matrix X;
    setzero(&X);

    Double_Matrix Temp;
    setzero(&Temp);

    int i, j, ij, k, l, kl;

    for (i = 0, ij = 0; i < NUM_ORB; i++) {
        for (j = 0; j <= i; j++, ij++) {
            for (k = 0, kl = 0; k < NUM_ORB; k++) {
                for (l = 0; l <= k; l++, kl++) {
                    X(k,l) = X(l,k) = TEI(INDEX(ij,kl));
                  }
            }
            X = C0.transpose() * X * C0;
            for (k = 0, kl = 0; k < NUM_ORB; k++) {
                for (l = 0; l <= k; l++, kl++) {
                    Temp(kl,ij) = X(k,l);
                }
            }
        }
    }

    setzero(&TEI_MO);

    for (k = 0, kl = 0; k < NUM_ORB; k++) {
        for (l = 0; l <= k; l++, kl++) {
            for (i = 0, ij = 0; i < NUM_ORB; i++) {
                for (j = 0; j <= i; j++, ij++) {
                    X(i,j) = X(j,i) = Temp(kl,ij);
                }
            }
            X = C0.transpose() * X * C0;
            for (i = 0, ij = 0; i < NUM_ORB; i++){
                for (j = 0; j <= i; j++, ij++){
                    TEI_MO(INDEX(kl,ij)) = X(i,j);
                }
           }
        }
    }**/
}

void HartreeFock::TEI_Transform_N8() {
    /**
     AO to MO integral transformation using a single N^8 step
     Both Two Electron Integrals are stored in arrays, taking advantage of
     Permuational Symmetry
     **/

    int i, j, k, l, ijkl;
    int p, q, r, s, pqrs;

    setzero(&TEI_MO);

    for(i=0, ijkl=0; i < NUM_ORB; i++) {
    for(j=0; j <= i; j++) {
    for(k=0; k <= i; k++) {
    for(l=0; l <= (i==k ? j : k); l++, ijkl++) {

    for(p=0; p < NUM_ORB; p++) { // Over all orbitals
    for(q=0; q < NUM_ORB; q++) { // Over all orbitals

        for(r=0; r < NUM_ORB; r++) { // Over all orbitals
        for(s=0; s < NUM_ORB; s++) { // Over all orbitals

        pqrs = INDEX(INDEX(p,q),INDEX(r,s));

        TEI_MO(ijkl) += C0(p,i) * C0(q,j) * C0(r,k) * C0(s,l) * TEI(pqrs);

        }}
    }}}}}}
  }
