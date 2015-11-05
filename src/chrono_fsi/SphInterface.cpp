/*
 * SphInterface.cpp
 *
 *  Created on: Mar 2, 2015
 *      Author: Arman Pazouki
 */

#include "chrono_fsi/SphInterface.h"
#include "chrono/core/ChTransform.h"
#include "chrono_fsi/collideSphereSphere.cuh"
// Chrono Vehicle Include


chrono::ChVector<> ConvertRealToChVector(Real3 p3) {
  return chrono::ChVector<>(p3.x, p3.y, p3.z);
}
chrono::ChVector<> ConvertRealToChVector(Real4 p4) {
  return ConvertRealToChVector(mR3(p4));
}

chrono::ChQuaternion<> ConvertToChQuaternion(Real4 q4) {
  return chrono::ChQuaternion<>(q4.x, q4.y, q4.z, q4.w);
}

Real3 ConvertChVectorToR3(chrono::ChVector<> v3) {
  return mR3(v3.x, v3.y, v3.z);
}

Real4 ConvertChVectorToR4(chrono::ChVector<> v3, Real m) {
  return mR4(v3.x, v3.y, v3.z, m);
}

Real4 ConvertChQuaternionToR4(chrono::ChQuaternion<> q4) {
  return mR4(q4.e0, q4.e1, q4.e2, q4.e3);
}

Real3 Rotate_By_Quaternion(Real4 q4, Real3 BCE_Pos_local) {
  chrono::ChQuaternion<> chQ = ConvertToChQuaternion(q4);
  chrono::ChVector<> dumPos = chQ.Rotate(ConvertRealToChVector(BCE_Pos_local));
  return ConvertChVectorToR3(dumPos);
}

Real3 R3_LocalToGlobal(Real3 p3LF, chrono::ChVector<> pos, chrono::ChQuaternion<> rot) {
  chrono::ChVector<> p3GF = chrono::ChTransform<>::TransformLocalToParent(ConvertRealToChVector(p3LF), pos, rot);
  return ConvertChVectorToR3(p3GF);
}
//------------------------------------------------------------------------------------
void AddSphDataToChSystem(chrono::ChSystemParallelDVI& mphysicalSystem,
                          int& startIndexSph,
                          const thrust::host_vector<Real3>& posRadH,
                          const thrust::host_vector<Real4>& velMasH,
                          const SimParams& paramsH,
                          const NumberOfObjects& numObjects,
                          int collisionFamilly,
                          Real sphMarkerMass) {
  Real rad = 0.5 * paramsH.MULT_INITSPACE * paramsH.HSML;
  // NOTE: mass properties and shapes are all for sphere
  double volume = chrono::utils::CalcSphereVolume(rad);
  chrono::ChVector<> gyration = chrono::utils::CalcSphereGyration(rad).Get_Diag();
  double density = paramsH.rho0;
  double mass = sphMarkerMass;  // density * volume;
  double muFriction = 0;

  // int fId = 0; //fluid id

  // Create a common material
  chrono::ChSharedPtr<chrono::ChMaterialSurface> mat_g(new chrono::ChMaterialSurface);
  mat_g->SetFriction(muFriction);
  mat_g->SetCohesion(0);
  mat_g->SetCompliance(0.0);
  mat_g->SetComplianceT(0.0);
  mat_g->SetDampingF(0.2);

  const chrono::ChQuaternion<> rot = chrono::ChQuaternion<>(1, 0, 0, 0);

  startIndexSph = mphysicalSystem.Get_bodylist()->size();
  // openmp does not work here
  for (int i = 0; i < numObjects.numFluidMarkers; i++) {
    Real3 p3 = posRadH[i];
    Real4 vM4 = velMasH[i];
    chrono::ChVector<> pos = ConvertRealToChVector(p3);
    chrono::ChVector<> vel = ConvertRealToChVector(vM4);
    chrono::ChSharedBodyPtr body;
    body = chrono::ChSharedBodyPtr(new chrono::ChBody(new chrono::collision::ChCollisionModelParallel));
    body->SetMaterialSurface(mat_g);
    // body->SetIdentifier(fId);
    body->SetPos(pos);
    body->SetRot(rot);
    body->SetCollide(true);
    body->SetBodyFixed(false);
    body->SetMass(mass);
    body->SetInertiaXX(mass * gyration);

    body->GetCollisionModel()->ClearModel();

    // add collision geometry
    //	body->GetCollisionModel()->AddEllipsoid(size.x, size.y, size.z, pos, rot);
    //
    //	// add asset (for visualization)
    //	ChSharedPtr<ChEllipsoidShape> ellipsoid(new ChEllipsoidShape);
    //	ellipsoid->GetEllipsoidGeometry().rad = size;
    //	ellipsoid->Pos = pos;
    //	ellipsoid->Rot = rot;
    //
    //	body->GetAssets().push_back(ellipsoid);

    //	chrono::utils::AddCapsuleGeometry(body.get_ptr(), size.x, size.y);		// X
    //	chrono::utils::AddCylinderGeometry(body.get_ptr(), size.x, size.y);		// O
    //	chrono::utils::AddConeGeometry(body.get_ptr(), size.x, size.y); 		// X
    //	chrono::utils::AddBoxGeometry(body.get_ptr(), size);					// O
    chrono::utils::AddSphereGeometry(body.get_ptr(), rad);  // O
    //	chrono::utils::AddEllipsoidGeometry(body.get_ptr(), size);					// X

    body->GetCollisionModel()->SetFamily(collisionFamilly);
    body->GetCollisionModel()->SetFamilyMaskNoCollisionWithFamily(collisionFamilly);

    body->GetCollisionModel()->BuildModel();
    mphysicalSystem.AddBody(body);
  }
}
//------------------------------------------------------------------------------------
void AddHydroForce(chrono::ChSystemParallelDVI& mphysicalSystem,
                   int& startIndexSph,
                   const NumberOfObjects& numObjects) {
  // openmp does not work here
  std::vector<chrono::ChBody*>::iterator bodyIter = mphysicalSystem.Get_bodylist()->begin() + startIndexSph;
#pragma omp parallel for
  for (int i = 0; i < numObjects.numFluidMarkers; i++) {
    char forceTag[] = "hydrodynamics_force";
    chrono::ChSharedPtr<chrono::ChForce> hydroForce = (*(bodyIter + i))->SearchForce(forceTag);
    if (hydroForce.IsNull()) {
      hydroForce = chrono::ChSharedPtr<chrono::ChForce>(new chrono::ChForce);
      hydroForce->SetMode(FTYPE_FORCE);  // no need for this. It is the default option.
      (*(bodyIter + i))->AddForce(hydroForce);
      // ** or: hydroForce = ChSharedPtr<ChForce>(new ChForce());
      hydroForce->SetName(forceTag);
    }
  }
}
//------------------------------------------------------------------------------------
void UpdateSphDataInChSystem(chrono::ChSystemParallelDVI& mphysicalSystem,
                             const thrust::host_vector<Real3>& posRadH,
                             const thrust::host_vector<Real4>& velMasH,
                             const NumberOfObjects& numObjects,
                             int startIndexSph) {
#pragma omp parallel for
  for (int i = 0; i < numObjects.numFluidMarkers; i++) {
    Real3 p3 = posRadH[i];
    Real4 vM4 = velMasH[i];
    chrono::ChVector<> pos = ConvertRealToChVector(p3);
    chrono::ChVector<> vel = ConvertRealToChVector(vM4);

    int chSystemBodyId = startIndexSph + i;
    std::vector<chrono::ChBody*>::iterator ibody = mphysicalSystem.Get_bodylist()->begin() + chSystemBodyId;
    (*ibody)->SetPos(pos);
    (*ibody)->SetPos_dt(vel);
  }
}
//------------------------------------------------------------------------------------
void AddChSystemForcesToSphForces(thrust::host_vector<Real4>& derivVelRhoChronoH,
                                  const thrust::host_vector<Real4>& velMasH2,
                                  chrono::ChSystemParallelDVI& mphysicalSystem,
                                  const NumberOfObjects& numObjects,
                                  int startIndexSph,
                                  Real dT) {
  std::vector<chrono::ChBody*>::iterator bodyIter = mphysicalSystem.Get_bodylist()->begin() + startIndexSph;
#pragma omp parallel for
  for (int i = 0; i < numObjects.numFluidMarkers; i++) {
    chrono::ChVector<> v = ((chrono::ChBody*)(*(bodyIter + i)))->GetPos_dt();
    Real3 a3 = (mR3(v.x, v.y, v.z) - mR3(velMasH2[i])) / dT;  // f = m * a
    derivVelRhoChronoH[i] += mR4(a3, 0);                      // note, gravity force is also coming from rigid system
  }
}
//------------------------------------------------------------------------------------

void ClearArraysH(thrust::host_vector<Real3>& posRadH,  // do not set the size here since you are using push back later
                  thrust::host_vector<Real4>& velMasH,
                  thrust::host_vector<Real4>& rhoPresMuH) {
  posRadH.clear();
  velMasH.clear();
  rhoPresMuH.clear();
}
//------------------------------------------------------------------------------------

void ClearArraysH(thrust::host_vector<Real3>& posRadH,  // do not set the size here since you are using push back later
                  thrust::host_vector<Real4>& velMasH,
                  thrust::host_vector<Real4>& rhoPresMuH,
                  thrust::host_vector<uint>& bodyIndex,
                  thrust::host_vector< ::int3>& referenceArray) {
  ClearArraysH(posRadH, velMasH, rhoPresMuH);
  bodyIndex.clear();
  referenceArray.clear();
}
//------------------------------------------------------------------------------------

void CopyD2H(thrust::host_vector<Real4>& derivVelRhoChronoH, const thrust::device_vector<Real4>& derivVelRhoD) {
  //	  assert(derivVelRhoChronoH.size() == derivVelRhoD.size() && "Error! size mismatch host and device");
  if (derivVelRhoChronoH.size() != derivVelRhoD.size()) {
    printf("\n\n\n\n Error! size mismatch host and device \n\n\n\n");
  }
  thrust::copy(derivVelRhoD.begin(), derivVelRhoD.end(), derivVelRhoChronoH.begin());
}

//------------------------------------------------------------------------------------
void CountNumContactsPerSph(thrust::host_vector<short int>& numContactsOnAllSph,
                            const chrono::ChSystemParallelDVI& mphysicalSystem,
                            const NumberOfObjects& numObjects,
                            int startIndexSph) {
  int numContacts = mphysicalSystem.data_manager->host_data.bids_rigid_rigid.size();
#pragma omp parallel for
  for (int i = 0; i < numContacts; i++) {
    chrono::int2 ids = mphysicalSystem.data_manager->host_data.bids_rigid_rigid[i];
    if (ids.x > startIndexSph)
      numContactsOnAllSph[ids.x - startIndexSph] += 1;
    if (ids.y > startIndexSph)
      numContactsOnAllSph[ids.y - startIndexSph] += 1;
  }
}

//------------------------------------------------------------------------------------
void CopyForceSphToChSystem(chrono::ChSystemParallelDVI& mphysicalSystem,
                            const NumberOfObjects& numObjects,
                            int startIndexSph,
                            const thrust::device_vector<Real4>& derivVelRhoD,
                            const thrust::host_vector<short int>& numContactsOnAllSph,
                            Real sphMass) {
  std::vector<chrono::ChBody*>::iterator bodyIter = mphysicalSystem.Get_bodylist()->begin() + startIndexSph;
#pragma omp parallel for
  for (int i = 0; i < numObjects.numFluidMarkers; i++) {
    char forceTag[] = "hydrodynamics_force";
    chrono::ChSharedPtr<chrono::ChForce> hydroForce = (*(bodyIter + i))->SearchForce(forceTag);
    //		if (!hydroForce.IsNull())
    //			hydroForce->SetMforce(0);
    //
    //		if (numContactsOnAllSph[i] == 0) continue;
    //    assert(!hydroForce.IsNull() && "Error! sph marker does not have hyroforce tag in ChSystem");
    if (hydroForce.IsNull()) {
      printf("\n\n\n\n Error! sph marker does not have hyroforce tag in ChSystem \n\n\n\n");
    }

    Real4 mDerivVelRho = derivVelRhoD[i];
    Real3 forceSphMarker = mR3(mDerivVelRho) * sphMass;
    chrono::ChVector<> f3 = ConvertRealToChVector(forceSphMarker);
    hydroForce->SetMforce(f3.Length());
    f3.Normalize();
    hydroForce->SetDir(f3);
  }
}

//------------------------------------------------------------------------------------

void CopyCustomChSystemPosVel2HostThrust(thrust::host_vector<Real3>& posRadH,
                                         thrust::host_vector<Real4>& velMasH,
                                         chrono::ChSystemParallelDVI& mphysicalSystem,
                                         const NumberOfObjects& numObjects,
                                         int startIndexSph,
                                         const thrust::host_vector<short int>& numContactsOnAllSph) {
  std::vector<chrono::ChBody*>::iterator bodyIter = mphysicalSystem.Get_bodylist()->begin() + startIndexSph;
#pragma omp parallel for
  for (int i = 0; i < numObjects.numFluidMarkers; i++) {
    if (numContactsOnAllSph[i] == 0)
      continue;
    chrono::ChVector<> pos = (*(bodyIter + i))->GetPos();
    posRadH[i] = mR3(pos.x, pos.y, pos.z);
    chrono::ChVector<> vel = (*(bodyIter + i))->GetPos_dt();
    Real mass = velMasH[i].w;
    velMasH[i] = mR4(vel.x, vel.y, vel.z, mass);
  }
}
//------------------------------------------------------------------------------------

void CopyH2DPosVel(thrust::device_vector<Real3>& posRadD,
                   thrust::device_vector<Real4>& velMasD,
                   const thrust::host_vector<Real3>& posRadH,
                   const thrust::host_vector<Real4>& velMasH) {
  //  assert(posRadH.size() == posRadD.size() && "Error! size mismatch host and device");
  if (posRadH.size() != posRadD.size()) {
    printf("\n\n\n\n Error! size mismatch host and device \n\n\n\n");
  }
  thrust::copy(posRadH.begin(), posRadH.end(), posRadD.begin());
  thrust::copy(velMasH.begin(), velMasH.end(), velMasD.begin());
}

//------------------------------------------------------------------------------------

void CopyD2HPosVel(thrust::host_vector<Real3>& posRadH,
                   thrust::host_vector<Real4>& velMasH,
                   const thrust::host_vector<Real3>& posRadD,
                   const thrust::host_vector<Real4>& velMasD) {
  //  assert(posRadH.size() == posRadD.size() && "Error! size mismatch host and device");
  if (posRadH.size() != posRadD.size()) {
    printf("\n\n\n\n Error! size mismatch host and device \n\n\n\n");
  }
  thrust::copy(posRadD.begin(), posRadD.end(), posRadH.begin());
  thrust::copy(velMasD.begin(), velMasD.end(), velMasH.begin());
}

//------------------------------------------------------------------------------------

void CopyH2D(thrust::device_vector<Real4>& derivVelRhoD, const thrust::host_vector<Real4>& derivVelRhoChronoH) {
  //  assert(derivVelRhoChronoH.size() == derivVelRhoD.size() && "Error! size mismatch host and device");
  if (derivVelRhoChronoH.size() != derivVelRhoD.size()) {
    printf("\n\n\n\n Error! size mismatch host and device \n\n\n\n");
  }
  thrust::copy(derivVelRhoChronoH.begin(), derivVelRhoChronoH.end(), derivVelRhoD.begin());
}
//------------------------------------------------------------------------------------

void CopySys2D(thrust::device_vector<Real3>& posRadD,
               chrono::ChSystemParallelDVI& mphysicalSystem,
               const NumberOfObjects& numObjects,
               int startIndexSph) {
  thrust::host_vector<Real3> posRadH(numObjects.numFluidMarkers);
  std::vector<chrono::ChBody*>::iterator bodyIter = mphysicalSystem.Get_bodylist()->begin() + startIndexSph;
#pragma omp parallel for
  for (int i = 0; i < numObjects.numFluidMarkers; i++) {
    chrono::ChVector<> p = ((chrono::ChBody*)(*(bodyIter + i)))->GetPos();
    posRadH[i] += mR3(p.x, p.y, p.z);
  }
  thrust::copy(posRadH.begin(), posRadH.end(), posRadD.begin());
}
//------------------------------------------------------------------------------------

void CopyD2H(thrust::host_vector<Real3>& posRadH,  // do not set the size here since you are using push back later
             thrust::host_vector<Real4>& velMasH,
             thrust::host_vector<Real4>& rhoPresMuH,
             const thrust::device_vector<Real3>& posRadD,
             const thrust::device_vector<Real4>& velMasD,
             const thrust::device_vector<Real4>& rhoPresMuD) {
  //  assert(posRadH.size() == posRadD.size() && "Error! size mismatch host and device");
  if (posRadH.size() != posRadD.size()) {
    printf("\n\n\n\n Error! size mismatch host and device \n\n\n\n");
  }
  thrust::copy(posRadD.begin(), posRadD.end(), posRadH.begin());
  thrust::copy(velMasD.begin(), velMasD.end(), velMasH.begin());
  thrust::copy(rhoPresMuD.begin(), rhoPresMuD.end(), rhoPresMuH.begin());
}

//------------------------------------------------------------------------------------
// FSI_Bodies_Index_H[i] is the the index of the i_th sph represented rigid body in ChSystem
void Add_Rigid_ForceTorques_To_ChSystem(chrono::ChSystemParallelDVI& mphysicalSystem,
                                        const thrust::device_vector<Real3>& rigid_FSI_ForcesD,
                                        const thrust::device_vector<Real3>& rigid_FSI_TorquesD,
                                        const std::vector<chrono::ChSharedPtr<chrono::ChBody> >& FSI_Bodies) {
  int numRigids = FSI_Bodies.size();
  std::vector<chrono::ChBody*>::iterator myIter = mphysicalSystem.Get_bodylist()->begin();
#pragma omp parallel for
  for (int i = 0; i < numRigids; i++) {
    chrono::ChSharedPtr<chrono::ChBody> bodyPtr = FSI_Bodies[i];
    bodyPtr->Empty_forces_accumulators();
    Real3 mforce = rigid_FSI_ForcesD[i];
    bodyPtr->Accumulate_force(ConvertRealToChVector(mforce), bodyPtr->GetPos(), false);

    Real3 mtorque = rigid_FSI_TorquesD[i];
    bodyPtr->Accumulate_torque(ConvertRealToChVector(mtorque), false);
  }
}

//------------------------------------------------------------------------------------
// FSI_Bodies_Index_H[i] is the the index of the i_th sph represented rigid body in ChSystem
void Copy_External_To_ChSystem(chrono::ChSystemParallelDVI& mphysicalSystem,
                               const thrust::host_vector<Real3>& pos_ChSystemBackupH,
                               const thrust::host_vector<Real4>& quat_ChSystemBackupH,
                               const thrust::host_vector<Real3>& vel_ChSystemBackupH,
                               const thrust::host_vector<Real3>& omegaLRF_ChSystemBackupH) {
  int numBodies = mphysicalSystem.Get_bodylist()->size();
  //  assert(posRigidH.size() == numBodies && "Error!!! Size of the external data does not match the ChSystem");
  if (pos_ChSystemBackupH.size() != numBodies) {
    printf("\n\n\n\n Error!!! Size of the external data does not match the ChSystem \n\n\n\n");
  }
  std::vector<chrono::ChBody*>::iterator myIter = mphysicalSystem.Get_bodylist()->begin();
#pragma omp parallel for
  for (int i = 0; i < numBodies; i++) {
    chrono::ChBody* bodyPtr = *(myIter + i);
    bodyPtr->SetPos(ConvertRealToChVector(pos_ChSystemBackupH[i]));
    bodyPtr->SetRot(ConvertToChQuaternion(quat_ChSystemBackupH[i]));
    bodyPtr->SetPos_dt(ConvertRealToChVector(vel_ChSystemBackupH[i]));
    bodyPtr->SetWvel_par(ConvertRealToChVector(omegaLRF_ChSystemBackupH[i]));
  }
}
//------------------------------------------------------------------------------------
void Copy_ChSystem_to_External(thrust::host_vector<Real3>& pos_ChSystemBackupH,
                               thrust::host_vector<Real4>& quat_ChSystemBackupH,
                               thrust::host_vector<Real3>& vel_ChSystemBackupH,
                               thrust::host_vector<Real3>& omegaLRF_ChSystemBackupH,
                               chrono::ChSystemParallelDVI& mphysicalSystem) {
  int numBodies = mphysicalSystem.Get_bodylist()->size();
  pos_ChSystemBackupH.resize(numBodies);
  quat_ChSystemBackupH.resize(numBodies);
  vel_ChSystemBackupH.resize(numBodies);
  omegaLRF_ChSystemBackupH.resize(numBodies);
  std::vector<chrono::ChBody*>::iterator myIter = mphysicalSystem.Get_bodylist()->begin();
#pragma omp parallel for
  for (int i = 0; i < numBodies; i++) {
    chrono::ChBody* bodyPtr = *(myIter + i);
    pos_ChSystemBackupH[i] = ConvertChVectorToR3(bodyPtr->GetPos());
    quat_ChSystemBackupH[i] = ConvertChQuaternionToR4(bodyPtr->GetRot());
    vel_ChSystemBackupH[i] = ConvertChVectorToR3(bodyPtr->GetPos_dt());
    omegaLRF_ChSystemBackupH[i] = ConvertChVectorToR3(bodyPtr->GetWvel_par());
  }
}

//------------------------------------------------------------------------------------
// FSI_Bodies_Index_H[i] is the the index of the i_th sph represented rigid body in ChSystem
void Copy_fsiBodies_ChSystem_to_FluidSystem(thrust::device_vector<Real3>& posRigid_fsiBodies_D,
                                            thrust::device_vector<Real4>& q_fsiBodies_D,
                                            thrust::device_vector<Real4>& velMassRigid_fsiBodies_D,
                                            thrust::device_vector<Real3>& rigidOmegaLRF_fsiBodies_D,
                                            thrust::host_vector<Real3>& posRigid_fsiBodies_H,
                                            thrust::host_vector<Real4>& q_fsiBodies_H,
                                            thrust::host_vector<Real4>& velMassRigid_fsiBodies_H,
                                            thrust::host_vector<Real3>& rigidOmegaLRF_fsiBodies_H,
                                            const std::vector<chrono::ChSharedPtr<chrono::ChBody> >& FSI_Bodies,
                                            chrono::ChSystemParallelDVI& mphysicalSystem) {
  int num_fsiBodies_Rigids = FSI_Bodies.size();
  //	  assert(posRigid_fsiBodies_D.size() == num_fsiBodies_Rigids && "Error!!! number of fsi bodies that are tracked
  // does not match the array size");

  if (posRigid_fsiBodies_D.size() != num_fsiBodies_Rigids || posRigid_fsiBodies_H.size() != num_fsiBodies_Rigids) {
    printf("\n\n\n\n Error!!! number of fsi bodies that are tracked does not match the array size \n\n\n\n");
  }
  std::vector<chrono::ChBody*>::iterator myIter = mphysicalSystem.Get_bodylist()->begin();
#pragma omp parallel for
  for (int i = 0; i < num_fsiBodies_Rigids; i++) {
    chrono::ChSharedPtr<chrono::ChBody> bodyPtr = FSI_Bodies[i];
    posRigid_fsiBodies_H[i] = ConvertChVectorToR3(bodyPtr->GetPos());
    q_fsiBodies_H[i] = ConvertChQuaternionToR4(bodyPtr->GetRot());
    velMassRigid_fsiBodies_H[i] = ConvertChVectorToR4(bodyPtr->GetPos_dt(), bodyPtr->GetMass());
    rigidOmegaLRF_fsiBodies_H[i] = ConvertChVectorToR3(bodyPtr->GetWacc_loc());
  }

  thrust::copy(posRigid_fsiBodies_H.begin(), posRigid_fsiBodies_H.end(), posRigid_fsiBodies_D.begin());
  thrust::copy(q_fsiBodies_H.begin(), q_fsiBodies_H.end(), q_fsiBodies_D.begin());
  thrust::copy(velMassRigid_fsiBodies_H.begin(), velMassRigid_fsiBodies_H.end(), velMassRigid_fsiBodies_D.begin());
  thrust::copy(rigidOmegaLRF_fsiBodies_H.begin(), rigidOmegaLRF_fsiBodies_H.end(), rigidOmegaLRF_fsiBodies_D.begin());
}




#ifdef CHRONO_OPENGL
#undef CHRONO_OPENGL
#endif
#ifdef CHRONO_OPENGL
#include "chrono_opengl/ChOpenGLWindow.h"
chrono::opengl::ChOpenGLWindow& gl_window = chrono::opengl::ChOpenGLWindow::getInstance();
#endif


// =============================================================================

void InitializeChronoGraphics(chrono::ChSystemParallelDVI& mphysicalSystem) {
    //	Real3 domainCenter = 0.5 * (paramsH.cMin + paramsH.cMax);
    //	ChVector<> CameraLocation = ChVector<>(2 * paramsH.cMax.x, 2 * paramsH.cMax.y, 2 * paramsH.cMax.z);
    //	ChVector<> CameraLookAt = ChVector<>(domainCenter.x, domainCenter.y, domainCenter.z);
    chrono::ChVector<> CameraLocation = chrono::ChVector<>(0, -10, 0);
    chrono::ChVector<> CameraLookAt = chrono::ChVector<>(0, 0, 0);

#ifdef CHRONO_OPENGL
    gl_window.Initialize(1280, 720, "HMMWV", &mphysicalSystem);
    gl_window.SetCamera(CameraLocation, CameraLookAt, ChVector<>(0, 0, 1));
    gl_window.SetRenderMode(opengl::WIREFRAME);

// Uncomment the following two lines for the OpenGL manager to automatically un the simulation in an infinite loop.

// gl_window.StartDrawLoop(paramsH.dT);
// return 0;
#endif

#if irrlichtVisualization
    // Create the Irrlicht visualization (open the Irrlicht device,
    // bind a simple user interface, etc. etc.)
    application = std::shared_ptr<ChIrrApp>(
        new ChIrrApp(&mphysicalSystem, L"Bricks test", core::dimension2d<u32>(800, 600), false, true));
    //	ChIrrApp application(&mphysicalSystem, L"Bricks test",core::dimension2d<u32>(800,600),false, true);

    // Easy shortcuts to add camera, lights, logo and sky in Irrlicht scene:
    ChIrrWizard::add_typical_Logo(application->GetDevice());
    //		ChIrrWizard::add_typical_Sky   (application->GetDevice());
    ChIrrWizard::add_typical_Lights(
        application->GetDevice(), core::vector3df(14.0f, 44.0f, -18.0f), core::vector3df(-3.0f, 8.0f, 6.0f), 59, 40);
    ChIrrWizard::add_typical_Camera(
        application->GetDevice(),
        core::vector3df(CameraLocation.x, CameraLocation.y, CameraLocation.z),
        core::vector3df(CameraLookAt.x, CameraLookAt.y, CameraLookAt.z));  //   (7.2,30,0) :  (-3,12,-8)
    // Use this function for adding a ChIrrNodeAsset to all items
    // If you need a finer control on which item really needs a visualization proxy in
    // Irrlicht, just use application->AssetBind(myitem); on a per-item basis.
    application->AssetBindAll();
    // Use this function for 'converting' into Irrlicht meshes the assets
    // into Irrlicht-visualizable meshes
    application->AssetUpdateAll();

    application->SetStepManage(true);
#endif
}
// =============================================================================

int DoStepChronoSystem(chrono::ChSystemParallelDVI& mphysicalSystem,
                       chrono::vehicle::ChWheeledVehicleAssembly* mVehicle,
                       Real dT,
                       double mTime,
                       double time_hold_vehicle) {
    if (haveVehicle) {
        // Release the vehicle chassis at the end of the hold time.

        if (mVehicle->GetVehicle()->GetChassis()->GetBodyFixed() && mTime > time_hold_vehicle) {
            mVehicle->GetVehicle()->GetChassis()->SetBodyFixed(false);
            for (int i = 0; i < 2 * mVehicle->GetVehicle()->GetNumberAxles(); i++) {
                mVehicle->GetVehicle()->GetWheelBody(i)->SetBodyFixed(false);
            }
        }

        // Update vehicle
        mVehicle->Update(mTime);
    }

#if irrlichtVisualization
    Real3 domainCenter = 0.5 * (paramsH.cMin + paramsH.cMax);
    if (!(application->GetDevice()->run()))
        return 0;
    application->SetTimestep(dT);
    application->GetVideoDriver()->beginScene(true, true, SColor(255, 140, 161, 192));
    ChIrrTools::drawGrid(application->GetVideoDriver(),
                         2 * paramsH.HSML,
                         2 * paramsH.HSML,
                         50,
                         50,
                         ChCoordsys<>(ChVector<>(domainCenter.x, paramsH.worldOrigin.y, domainCenter.z),
                                      Q_from_AngAxis(CH_C_PI / 2, VECT_X)),
                         video::SColor(50, 90, 90, 150),
                         true);
    application->DrawAll();
    application->DoStep();
    application->GetVideoDriver()->endScene();
#else
#ifdef CHRONO_OPENGL
    if (gl_window.Active()) {
        gl_window.DoStepDynamics(dT);
        gl_window.Render();
    }
#else
    mphysicalSystem.DoStepDynamics(dT);
#endif
#endif
    return 1;
}
//------------------------------------------------------------------------------------
void DoStepDynamics_FSI(
		chrono::ChSystemParallelDVI& mphysicalSystem,
		chrono::vehicle::ChWheeledVehicleAssembly* mVehicle,
		thrust::device_vector<Real3>& posRadD,
                        thrust::device_vector<Real4>& velMasD,
                        thrust::device_vector<Real3>& vel_XSPH_D,
                        thrust::device_vector<Real4>& rhoPresMuD,

                        thrust::device_vector<Real3>& posRadD2,
                        thrust::device_vector<Real4>& velMasD2,
                        thrust::device_vector<Real4>& rhoPresMuD2,

                        thrust::device_vector<Real4>& derivVelRhoD,
                        thrust::device_vector<uint> & rigidIdentifierD,
                        thrust::device_vector<Real3> rigidSPH_MeshPos_LRF_D,

                        thrust::device_vector<Real3>& posRigid_fsiBodies_D,
                        thrust::device_vector<Real4>& q_fsiBodies_D,
                        thrust::device_vector<Real4>& velMassRigid_fsiBodies_D,
                        thrust::device_vector<Real3>& omegaLRF_fsiBodies_D,

                        thrust::device_vector<Real3>& posRigid_fsiBodies_D2,
                        thrust::device_vector<Real4>& q_fsiBodies_D2,
                        thrust::device_vector<Real4>& velMassRigid_fsiBodies_D2,
                        thrust::device_vector<Real3>& omegaLRF_fsiBodies_D2,

                        thrust::host_vector<Real3>& pos_ChSystemBackupH,
                        thrust::host_vector<Real4>& quat_ChSystemBackupH,
                        thrust::host_vector<Real3>& vel_ChSystemBackupH,
                        thrust::host_vector<Real3>& omegaLRF_ChSystemBackupH,

                        thrust::host_vector<Real3>& posRigid_fsiBodies_dummyH,
                        thrust::host_vector<Real4>& q_fsiBodies_dummyH,
                        thrust::host_vector<Real4>& velMassRigid_fsiBodies_dummyH,
                        thrust::host_vector<Real3>& omegaLRF_fsiBodies_dummyH,

                        thrust::device_vector<Real3>& rigid_FSI_ForcesD,
                        thrust::device_vector<Real3>& rigid_FSI_TorquesD,

                        thrust::device_vector<uint>& bodyIndexD,
                        std::vector<chrono::ChSharedPtr<chrono::ChBody>> & FSI_Bodies,
                        const thrust::host_vector<int3>& referenceArray,
                        const NumberOfObjects& numObjects,
                        const SimParams& paramsH,
                        Real sphMarkerMass,
                        double mTime,
                        double time_hold_vehicle,
                        int tStep) {
  chrono::ChTimerParallel doStep_timer;

  Copy_ChSystem_to_External(
      pos_ChSystemBackupH, quat_ChSystemBackupH, vel_ChSystemBackupH, omegaLRF_ChSystemBackupH, mphysicalSystem);
//**********************************
#if haveFluid

  InitSystem(paramsH, numObjects);
  // ** initialize host mid step data
  thrust::copy(posRadD.begin(), posRadD.end(), posRadD2.begin());
  thrust::copy(velMasD.begin(), velMasD.end(), velMasD2.begin());
  thrust::copy(rhoPresMuD2.begin(), rhoPresMuD2.end(), rhoPresMuD.begin());

  FillMyThrust4(derivVelRhoD, mR4(0));
#endif
//**********************************
// ******************
// ******************
// ******************
// ******************
// ****************** RK2: 1/2
#if haveFluid

  doStep_timer.start("half_step_dynamic_fsi_12");
  // //assumes ...D2 is a copy of ...D

  IntegrateSPH(derivVelRhoD,
               posRadD2,
               velMasD2,
               rhoPresMuD2,
               posRadD,
               velMasD,
               vel_XSPH_D,
               rhoPresMuD,
               bodyIndexD,
               referenceArray,
               numObjects,
               paramsH,
               0.5 * paramsH.dT);

  Rigid_Forces_Torques(rigid_FSI_ForcesD,
                       rigid_FSI_TorquesD,
                       posRadD,
                       posRigid_fsiBodies_D,
                       derivVelRhoD,
                       rigidIdentifierD,
                       numObjects,
                       sphMarkerMass);

  doStep_timer.stop("half_step_dynamic_fsi_12");

  doStep_timer.start("fsi_copy_force_fluid2ChSystem_12");
  Add_Rigid_ForceTorques_To_ChSystem(mphysicalSystem, rigid_FSI_ForcesD, rigid_FSI_TorquesD, FSI_Bodies);
  doStep_timer.stop("fsi_copy_force_fluid2ChSystem_12");
#endif

  doStep_timer.start("stepDynamic_mbd_12");
  mTime += 0.5 * paramsH.dT;
  DoStepChronoSystem(mphysicalSystem,
		  mVehicle,
                     0.5 * paramsH.dT,
                     mTime,
                     time_hold_vehicle);  // Keep only this if you are just interested in the rigid sys

  doStep_timer.stop("stepDynamic_mbd_12");

#if haveFluid
  doStep_timer.start("fsi_copy_posVel_ChSystem2fluid_12");

  Copy_fsiBodies_ChSystem_to_FluidSystem(posRigid_fsiBodies_D2,
                                         q_fsiBodies_D2,
                                         velMassRigid_fsiBodies_D2,
                                         omegaLRF_fsiBodies_D2,
                                         posRigid_fsiBodies_dummyH,
                                         q_fsiBodies_dummyH,
                                         velMassRigid_fsiBodies_dummyH,
                                         omegaLRF_fsiBodies_dummyH,
                                         FSI_Bodies,
                                         mphysicalSystem);
  doStep_timer.stop("fsi_copy_posVel_ChSystem2fluid_12");

  doStep_timer.start("update_marker_pos_12");
  UpdateRigidMarkersPosition(posRadD2,
                             velMasD2,
                             rigidSPH_MeshPos_LRF_D,
                             rigidIdentifierD,
                             posRigid_fsiBodies_D2,
                             q_fsiBodies_D2,
                             velMassRigid_fsiBodies_D2,
                             omegaLRF_fsiBodies_D2,
                             numObjects);
  doStep_timer.stop("update_marker_pos_12");
  // ******************
  // ******************
  // ******************
  // ******************
  // ****************** RK2: 2/2
  FillMyThrust4(derivVelRhoD, mR4(0));

  // //assumes ...D2 is a copy of ...D
  IntegrateSPH(derivVelRhoD,
               posRadD,
               velMasD,
               rhoPresMuD,
               posRadD2,
               velMasD2,
               vel_XSPH_D,
               rhoPresMuD2,
               bodyIndexD,
               referenceArray,
               numObjects,
               paramsH,
               paramsH.dT);

  Rigid_Forces_Torques(rigid_FSI_ForcesD,
                       rigid_FSI_TorquesD,
                       posRadD2,
                       posRigid_fsiBodies_D2,
                       derivVelRhoD,
                       rigidIdentifierD,
                       numObjects,
                       sphMarkerMass);
  Add_Rigid_ForceTorques_To_ChSystem(
      mphysicalSystem, rigid_FSI_ForcesD, rigid_FSI_TorquesD, FSI_Bodies);  // Arman: take care of this

#endif
  mTime -= 0.5 * paramsH.dT;

  // Arman: do it so that you don't need gpu when you don't have fluid
  Copy_External_To_ChSystem(
      mphysicalSystem, pos_ChSystemBackupH, quat_ChSystemBackupH, vel_ChSystemBackupH, omegaLRF_ChSystemBackupH);

  mTime += paramsH.dT;

  DoStepChronoSystem(mphysicalSystem,
		  mVehicle,
                     1.0 * paramsH.dT,
                     mTime,
                     time_hold_vehicle);

#if haveFluid

  Copy_fsiBodies_ChSystem_to_FluidSystem(posRigid_fsiBodies_D,
                                         q_fsiBodies_D,
                                         velMassRigid_fsiBodies_D,
                                         omegaLRF_fsiBodies_D,
                                         posRigid_fsiBodies_dummyH,
                                         q_fsiBodies_dummyH,
                                         velMassRigid_fsiBodies_dummyH,
                                         omegaLRF_fsiBodies_dummyH,
                                         FSI_Bodies,
                                         mphysicalSystem);
  UpdateRigidMarkersPosition(posRadD,
                             velMasD,
                             rigidSPH_MeshPos_LRF_D,
                             rigidIdentifierD,
                             posRigid_fsiBodies_D,
                             q_fsiBodies_D,
                             velMassRigid_fsiBodies_D,
                             omegaLRF_fsiBodies_D,
                             numObjects);

  if ((tStep % 10 == 0) && (paramsH.densityReinit != 0)) {
    DensityReinitialization(posRadD, velMasD, rhoPresMuD, numObjects.numAllMarkers, paramsH.gridSize);
  }

#endif
  doStep_timer.PrintReport();

  // ****************** End RK2
}