//------------------------------------------------------------------------------
//  BulletPhysicsCloth.cc
//------------------------------------------------------------------------------
#include "Pre.h"
#include "Core/Main.h"
#include "Gfx/Gfx.h"
#include "Dbg/Dbg.h"
#include "Input/Input.h"
#include "Core/Time/Clock.h"
#include "PhysicsCommon/Physics.h"
#include "PhysicsCommon/CameraHelper.h"
#include "PhysicsCommon/ShapeRenderer.h"
#include "shaders.h"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/random.hpp"

using namespace Oryol;

static const float SphereRadius = 1.0f;
static const float BoxSize = 1.5f;

class BulletPhysicsClothApp : public App {
public:
    AppState::Code OnInit();
    AppState::Code OnRunning();
    AppState::Code OnCleanup();

    Duration updatePhysics();
    Duration updateInstanceData();

    int frameIndex = 0;
    TimePoint lapTimePoint;
    CameraHelper camera;
    ShapeRenderer shapeRenderer;
    ColorShader::ColorVSParams colorVSParams;
    ColorShader::ColorFSParams colorFSParams;
    ShadowShader::ShadowVSParams shadowVSParams;
    glm::mat4 lightProjView;

    Id planeShape;
    Id boxShape;
    Id sphereShape;
    Id groundRigidBody;
    Id clothSoftBody;
    static const int MaxNumBodies = 256;
    int numBodies = 0;
    StaticArray<Id, MaxNumBodies> bodies;
};
OryolMain(BulletPhysicsClothApp);

//------------------------------------------------------------------------------
AppState::Code
BulletPhysicsClothApp::OnInit() {
    auto gfxSetup = GfxSetup::WindowMSAA4(800, 600, "BulletPhysicsBasic");
    Gfx::Setup(gfxSetup);
    this->colorFSParams.ShadowMapSize = glm::vec2(float(this->shapeRenderer.ShadowMapSize));

    // instanced shape rendering helper class
    this->shapeRenderer.ColorShader = Gfx::CreateResource(ColorShader::Setup());
    this->shapeRenderer.ColorShaderInstanced = Gfx::CreateResource(ColorShaderInstanced::Setup());
    this->shapeRenderer.ShadowShader = Gfx::CreateResource(ShadowShader::Setup());
    this->shapeRenderer.ShadowShaderInstanced = Gfx::CreateResource(ShadowShaderInstanced::Setup());
    this->shapeRenderer.SphereRadius = SphereRadius;
    this->shapeRenderer.BoxSize = BoxSize;
    this->shapeRenderer.Setup(gfxSetup);

    // setup directional light (for lighting and shadow rendering)
    glm::mat4 lightView = glm::lookAt(glm::vec3(50.0f, 50.0f, -50.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    // this shifts the post-projection Z coordinate into the range 0..1 (like D3D), 
    // instead of -1..+1 (like OpenGL), which makes sure that objects in the
    // range -1.0 to +1.0 are not clipped away in D3D, the shadow map lookup in D3D
    // also needs to invert the Y coordinate (that's handled in the pixel shader
    // where the lookup happens)
    glm::mat4 lightProj = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, 1.0f));
    lightProj = glm::scale(lightProj, glm::vec3(1.0f, 1.0f, 0.5f));
    lightProj = lightProj * glm::ortho(-75.0f, +75.0f, -75.0f, +75.0f, 1.0f, 400.0f);
    this->lightProjView = lightProj * lightView;
    this->colorFSParams.LightDir = glm::vec3(glm::column(glm::inverse(lightView), 2));

    // setup the initial physics world
    Physics::Setup();
    this->boxShape = Physics::Create(CollideShapeSetup::Box(glm::vec3(BoxSize)));
    this->sphereShape = Physics::Create(CollideShapeSetup::Sphere(SphereRadius));
    this->planeShape = Physics::Create(CollideShapeSetup::Plane(glm::vec4(0, 1, 0, 0)));

    // create a fixed ground rigid body
    this->groundRigidBody = Physics::Create(RigidBodySetup::FromShape(this->planeShape, glm::mat4(1.0f), 0.0f, 0.25f));
    Physics::Add(this->groundRigidBody);

    // create cloth shape
    auto clothSetup = SoftBodySetup::Patch();
    this->clothSoftBody = Physics::Create(clothSetup);
    Physics::Add(this->clothSoftBody);

    Input::Setup();
    Dbg::Setup();
    this->camera.Setup();
    this->lapTimePoint = Clock::Now();
    return App::OnInit();
}

//------------------------------------------------------------------------------
AppState::Code
BulletPhysicsClothApp::OnRunning() {
    Duration frameTime = Clock::LapTime(this->lapTimePoint);
    Duration physicsTime = this->updatePhysics();
    Duration instUpdTime = this->updateInstanceData();
    this->camera.Update();

    // the shadow pass
    this->shadowVSParams.MVP = this->lightProjView;
    this->shapeRenderer.DrawShadowPass(this->shadowVSParams);

    // the color pass
    Gfx::ApplyDefaultRenderTarget(ClearState::ClearAll(glm::vec4(0.2f, 0.4f, 0.8f, 1.0f), 1.0f, 0));

    // draw ground
    const glm::mat4 model = Physics::Transform(this->groundRigidBody);
    this->colorVSParams.Model = model;
    this->colorVSParams.MVP = this->camera.ViewProj * model;
    this->colorVSParams.LightMVP = lightProjView * model;
    this->colorVSParams.DiffColor = glm::vec3(0.5, 0.5, 0.5);
    this->colorFSParams.EyePos = this->camera.EyePos;
    this->shapeRenderer.DrawGround(this->colorVSParams, this->colorFSParams);

    // draw the dynamic shapes
    this->colorVSParams.Model = glm::mat4();
    this->colorVSParams.MVP = this->camera.ViewProj;
    this->colorVSParams.LightMVP = lightProjView;
    this->colorVSParams.DiffColor = glm::vec3(1.0f, 1.0f, 1.0f);
    this->shapeRenderer.DrawShapes(this->colorVSParams, this->colorFSParams);

    Dbg::PrintF("\n\r"
                "  Mouse left click + drag: rotate camera\n\r"
                "  Mouse wheel: zoom camera\n\r"
                "  P: pause/continue\n\n\r"
                "  Frame time:          %.4f ms\n\r"
                "  Physics time:        %.4f ms\n\r"
                "  Instance buffer upd: %.4f ms\n\r"
                "  Num Rigid Bodies:    %d\n\r",
                frameTime.AsMilliSeconds(),
                physicsTime.AsMilliSeconds(),
                instUpdTime.AsMilliSeconds(),
                this->numBodies);
    Dbg::DrawTextBuffer();
    Gfx::CommitFrame();
    return Gfx::QuitRequested() ? AppState::Cleanup : AppState::Running;
}

//------------------------------------------------------------------------------
AppState::Code
BulletPhysicsClothApp::OnCleanup() {
    // FIXME: Physics should just cleanup this stuff on shutdown!
    Physics::Remove(this->clothSoftBody);
    Physics::Destroy(this->clothSoftBody);
    Physics::Remove(this->groundRigidBody);
    Physics::Destroy(this->groundRigidBody);
    for (int i = 0; i < this->numBodies; i++) {
        Physics::Remove(this->bodies[i]);
        Physics::Destroy(this->bodies[i]);
    }
    Physics::Destroy(this->sphereShape);
    Physics::Destroy(this->boxShape);
    Physics::Destroy(this->planeShape);
    Physics::Discard();
    Dbg::Discard();
    Input::Discard();
    Gfx::Discard();
    return App::OnCleanup();
}

//------------------------------------------------------------------------------
Duration
BulletPhysicsClothApp::updatePhysics() {
    TimePoint physStartTime = Clock::Now();
    if (!this->camera.Paused) {
        // emit new rigid bodies
        this->frameIndex++;
        if ((this->frameIndex % 100) == 0) {
            if (this->numBodies < MaxNumBodies) {
                static const glm::mat4 tform = glm::translate(glm::mat4(), glm::vec3(0, 20, 0));
                Id newObj;
                if (this->numBodies & 1) {
                    newObj = Physics::Create(RigidBodySetup::FromShape(this->sphereShape, tform, 1.0f, 0.5f));
                }
                else {
                    newObj = Physics::Create(RigidBodySetup::FromShape(this->boxShape, tform, 1.0f, 0.5f));
                }
                Physics::Add(newObj);
                this->bodies[this->numBodies] = newObj;
                this->numBodies++;

                btRigidBody* body = Physics::RigidBody(newObj);
                glm::vec3 ang = glm::ballRand(10.0f);
                body->setAngularVelocity(btVector3(ang.x, ang.y, ang.z));
                body->setDamping(0.1f, 0.1f);
            }
        }
        Physics::Update(1.0f/60.0f);
    }
    return Clock::Since(physStartTime);
}

//------------------------------------------------------------------------------
Duration
BulletPhysicsClothApp::updateInstanceData() {
    TimePoint startTime = Clock::Now();
    this->shapeRenderer.BeginTransforms();
    for (int i = 0; i < numBodies; i++) {
        CollideShapeSetup::ShapeType type = Physics::RigidBodyShapeType(this->bodies[i]);
        if (CollideShapeSetup::SphereShape == type) {
            this->shapeRenderer.UpdateSphereTransform(Physics::Transform(this->bodies[i]));
        }
        else {
            this->shapeRenderer.UpdateBoxTransform(Physics::Transform(this->bodies[i]));
        }
    }
    this->shapeRenderer.EndTransforms();
    return Clock::Since(startTime);
}
