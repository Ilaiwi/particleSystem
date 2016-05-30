// Assignment 3, Part 1 and 2
//
// Modify this file according to the lab instructions.
//

#include "utils.h"
#include "utils2.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#ifdef WITH_TWEAKBAR
#include <AntTweakBar.h>
#endif // WITH_TWEAKBAR

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <cstdlib>
#include <algorithm>

// The attribute locations we will use in the vertex shader
enum AttributeLocation {
    POSITION = 0,
    COLOR = 1
};

// Struct for representing an indexed triangle mesh
struct Mesh {
    std::vector<glm::vec3> vertices;
    std::vector<glm::vec3> normals;
    std::vector<uint32_t> indices;
};

// Struct for representing a vertex array object (VAO) created from a
// mesh. Used for rendering.
struct MeshVAO {
    GLuint billboard_vertex_buffer;
    GLuint particles_position_buffer;
    GLuint particles_color_buffer;
    GLuint vao;
};

struct Particle{
    glm::vec3 pos, speed;
    unsigned char r,g,b,a; // Color
    float size, angle, weight;
    float life; // Remaining life of the particle. if <0 : dead and unused.
    float cameradistance; // *Squared* distance to the camera. if dead : -1.0f

    bool operator<(const Particle& that) const {
        // Sort in reverse order : far particles drawn first.
        return this->cameradistance > that.cameradistance;
    }
};


const int MaxParticles = 100000;
int LastUsedParticle = 0;

// Struct for resources and state
struct Context {
    int width;
    int height;
    float aspect;
    GLFWwindow *window;
    GLuint program;
    Trackball trackball;
    Mesh mesh;
    MeshVAO meshVAO;
    GLuint defaultVAO;
    GLuint cubemap;
    Particle ParticlesContainer[MaxParticles];
    int LastUsedParticle = 0;
    float delta;
};

static const GLfloat g_vertex_buffer_data[] = { 
         -0.5f, -0.5f, 0.0f,
          0.5f, -0.5f, 0.0f,
         -0.5f,  0.5f, 0.0f,
          0.5f,  0.5f, 0.0f,
    };

// Finds a Particle in ParticlesContainer which isn't used yet.
// (i.e. life < 0);
int FindUnusedParticle(Context &ctx){

    for(int i=ctx.LastUsedParticle; i<MaxParticles; i++){
        if (ctx.ParticlesContainer[i].life < 0){
            LastUsedParticle = i;
            return i;
        }
    }

    for(int i=0; i<ctx.LastUsedParticle; i++){
        if (ctx.ParticlesContainer[i].life < 0){
            ctx.LastUsedParticle = i;
            return i;
        }
    }

    return 0; // All particles are taken, override the first one
}

void SortParticles(Context &ctx){
    std::sort(&ctx.ParticlesContainer[0], &ctx.ParticlesContainer[MaxParticles]);
}


// Returns the value of an environment variable
std::string getEnvVar(const std::string &name)
{
    char const* value = std::getenv(name.c_str());
    if (value == nullptr) {
        return std::string();
    }
    else {
        return std::string(value);
    }
}

// Returns the absolute path to the shader directory
std::string shaderDir(void)
{
    std::string rootDir = getEnvVar("PARTICLESYSTEM_ROOT");
    if (rootDir.empty()) {
        std::cout << "Error: PARTICLESYSTEM_ROOT is not set." << std::endl;
        std::exit(EXIT_FAILURE);
    }
    return rootDir + "/model_viewer/src/shaders/";
}

// Returns the absolute path to the 3D model directory
std::string modelDir(void)
{
    std::string rootDir = getEnvVar("PARTICLESYSTEM_ROOT");
    if (rootDir.empty()) {
        std::cout << "Error: PARTICLESYSTEM_ROOT is not set." << std::endl;
        std::exit(EXIT_FAILURE);
    }
    return rootDir + "/model_viewer/3d_models/";
}

// Returns the absolute path to the cubemap texture directory
std::string cubemapDir(void)
{
    std::string rootDir = getEnvVar("PARTICLESYSTEM_ROOT");
    if (rootDir.empty()) {
        std::cout << "Error: PARTICLESYSTEM_ROOT is not set." << std::endl;
        std::exit(EXIT_FAILURE);
    }
    return rootDir + "/model_viewer/cubemaps/";
}

void loadMesh(const std::string &filename, Mesh *mesh)
{
    OBJMesh obj_mesh;
    objMeshLoad(obj_mesh, filename);
    mesh->vertices = obj_mesh.vertices;
    mesh->normals = obj_mesh.normals;
    mesh->indices = obj_mesh.indices;
}

void createMeshVAO(Context &ctx)
{
    // Generates and populates a VBO for the vertices
    glGenBuffers(1, &ctx.meshVAO.billboard_vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, ctx.meshVAO.billboard_vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_vertex_buffer_data), g_vertex_buffer_data, GL_STATIC_DRAW);

    // The VBO containing the positions and sizes of the particles
    glGenBuffers(1, &ctx.meshVAO.particles_position_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, ctx.meshVAO.particles_position_buffer);
    // Initialize with empty (NULL) buffer : it will be updated later, each frame.
    glBufferData(GL_ARRAY_BUFFER, MaxParticles * 4 * sizeof(GLfloat), NULL, GL_STREAM_DRAW);

    // The VBO containing the colors of the particles
    glGenBuffers(1, &ctx.meshVAO.particles_color_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, ctx.meshVAO.particles_color_buffer);
    // Initialize with empty (NULL) buffer : it will be updated later, each frame.
    glBufferData(GL_ARRAY_BUFFER, MaxParticles * 4 * sizeof(GLubyte), NULL, GL_STREAM_DRAW);
    
    glGenVertexArrays(1, &(ctx.meshVAO.vao));
    glBindVertexArray(ctx.meshVAO.vao);
    glBindBuffer(GL_ARRAY_BUFFER, ctx.meshVAO.billboard_vertex_buffer);

    glEnableVertexAttribArray(POSITION);
    glBindBuffer(GL_ARRAY_BUFFER, ctx.meshVAO.particles_position_buffer);
    glVertexAttribPointer(
            POSITION,                                // attribute. No particular reason for 1, but must match the layout in the shader.
            4,                                // size : x + y + z + size => 4
            GL_FLOAT,                         // type
            GL_FALSE,                         // normalized?
            0,                                // stride
            (void*)0                          // array buffer offset
    );

    glEnableVertexAttribArray(COLOR);
        glBindBuffer(GL_ARRAY_BUFFER, ctx.meshVAO.particles_color_buffer);
        glVertexAttribPointer(
            COLOR,                                // attribute. No particular reason for 1, but must match the layout in the shader.
            4,                                // size : r + g + b + a => 4
            GL_UNSIGNED_BYTE,                 // type
            GL_TRUE,                          // normalized?    *** YES, this means that the unsigned char[4] will be accessible with a vec4 (floats) in the shader ***
            0,                                // stride
            (void*)0                          // array buffer offset
    );

    glBindVertexArray(ctx.defaultVAO); 


}

void initializeTrackball(Context &ctx)
{
    double radius = double(std::min(ctx.width, ctx.height)) / 2.0;
    ctx.trackball.radius = radius;
    glm::vec2 center = glm::vec2(ctx.width, ctx.height) / 2.0f;
    ctx.trackball.center = center;
}

void init(Context &ctx)
{
    ctx.program = loadShaderProgram(shaderDir() + "particle.vert",
                                    shaderDir() + "particle.frag");

    // loadMesh((modelDir() + "gargo.obj"), &ctx.mesh);
    createMeshVAO(ctx);

    // Load cubemap texture(s)
    // ...

    initializeTrackball(ctx);
}

// MODIFY THIS FUNCTION
void drawMesh(Context &ctx, GLuint program, const MeshVAO &meshVAO)
{
    // Define uniforms
    glm::mat4 model = trackballGetRotationMatrix(ctx.trackball);
    glm::mat4 view = glm::lookAt(glm::vec3(0.0, 1.0, 4.0), glm::vec3(0.0, 0.0, -1.0), glm::vec3(0.0, 1.0, 0.0));
    glm::mat4 projection = glm::perspective(1.0f, 1.0f , 0.1f, 8.0f);
    glm::mat4 mv = view * model;
    glm::mat4 mvp = projection * mv;
    // ...
    glm::vec3 CameraPosition(glm::inverse(view)[3]);
    static GLfloat* g_particule_position_size_data = new GLfloat[MaxParticles * 4];
    static GLubyte* g_particule_color_data         = new GLubyte[MaxParticles * 4];
    for(int i=0; i<MaxParticles; i++){
        ctx.ParticlesContainer[i].life = -1.0f;
        ctx.ParticlesContainer[i].cameradistance = -1.0f;
    }

        // Generate 10 new particule each millisecond,
        // but limit this to 16 ms (60 fps), or if you have 1 long frame (1sec),
        // newparticles will be huge and the next frame even longer.
        int newparticles = (int)(ctx.delta*10000.0);
        if (newparticles > (int)(0.016f*10000.0))
            newparticles = (int)(0.016f*10000.0);
        
        for(int i=0; i<newparticles; i++){
            int particleIndex = FindUnusedParticle(ctx);
            ctx.ParticlesContainer[particleIndex].life = 5.0f; // This particle will live 5 seconds.
            ctx.ParticlesContainer[particleIndex].pos = glm::vec3(0,0,-20.0f);

            float spread = 1.5f;
            glm::vec3 maindir = glm::vec3(0.0f, 10.0f, 0.0f);
            // Very bad way to generate a random direction; 
            // See for instance http://stackoverflow.com/questions/5408276/python-uniform-spherical-distribution instead,
            // combined with some user-controlled parameters (main direction, spread, etc)
            glm::vec3 randomdir = glm::vec3(
                (rand()%2000 - 1000.0f)/1000.0f,
                (rand()%2000 - 1000.0f)/1000.0f,
                (rand()%2000 - 1000.0f)/1000.0f
            );
            
            ctx.ParticlesContainer[particleIndex].speed = maindir + randomdir*spread;


            // Very bad way to generate a random color
            ctx.ParticlesContainer[particleIndex].r = rand() % 256;
            ctx.ParticlesContainer[particleIndex].g = rand() % 256;
            ctx.ParticlesContainer[particleIndex].b = rand() % 256;
            ctx.ParticlesContainer[particleIndex].a = (rand() % 256) / 3;
            ctx.ParticlesContainer[particleIndex].size = (rand()%1000)/2000.0f + 0.1f;
            
        }

        // Simulate all particles
        int ParticlesCount = 0;
        for(int i=0; i<MaxParticles; i++){

            Particle& p = ctx.ParticlesContainer[i]; // shortcut

            if(p.life > 0.0f){

                // Decrease life
                p.life -= ctx.delta;
                if (p.life > 0.0f){

                    // Simulate simple physics : gravity only, no collisions
                    p.speed += glm::vec3(0.0f,-9.81f, 0.0f) * (float)ctx.delta * 0.5f;
                    p.pos += p.speed * (float)ctx.delta;
                    p.cameradistance = glm::length2( p.pos - CameraPosition );
                    //ParticlesContainer[i].pos += glm::vec3(0.0f,10.0f, 0.0f) * (float)ctx.delta;

                    // Fill the GPU buffer
                    g_particule_position_size_data[4*ParticlesCount+0] = p.pos.x;
                    g_particule_position_size_data[4*ParticlesCount+1] = p.pos.y;
                    g_particule_position_size_data[4*ParticlesCount+2] = p.pos.z;
                                                   
                    g_particule_position_size_data[4*ParticlesCount+3] = p.size;
                                                   
                    g_particule_color_data[4*ParticlesCount+0] = p.r;
                    g_particule_color_data[4*ParticlesCount+1] = p.g;
                    g_particule_color_data[4*ParticlesCount+2] = p.b;
                    g_particule_color_data[4*ParticlesCount+3] = p.a;

                }else{
                    // Particles that just died will be put at the end of the buffer in SortParticles();
                    p.cameradistance = -1.0f;
                }

                ParticlesCount++;

            }
        }

        SortParticles(ctx);

        glBindBuffer(GL_ARRAY_BUFFER, ctx.meshVAO.particles_position_buffer);
        glBufferData(GL_ARRAY_BUFFER, MaxParticles * 4 * sizeof(GLfloat), NULL, GL_STREAM_DRAW); // Buffer orphaning, a common way to improve streaming perf. See above link for details.
        glBufferSubData(GL_ARRAY_BUFFER, 0, ParticlesCount * sizeof(GLfloat) * 4, g_particule_position_size_data);

        glBindBuffer(GL_ARRAY_BUFFER, ctx.meshVAO.particles_color_buffer);
        glBufferData(GL_ARRAY_BUFFER, MaxParticles * 4 * sizeof(GLubyte), NULL, GL_STREAM_DRAW); // Buffer orphaning, a common way to improve streaming perf. See above link for details.
        glBufferSubData(GL_ARRAY_BUFFER, 0, ParticlesCount * sizeof(GLubyte) * 4, g_particule_color_data);


        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


    // Activate program
    glUseProgram(program);

    // Bind textures
    // ...

    // Pass uniforms
    glUniformMatrix4fv(glGetUniformLocation(program, "u_mv"), 1, GL_FALSE, &mv[0][0]);
    glUniformMatrix4fv(glGetUniformLocation(program, "u_mvp"), 1, GL_FALSE, &mvp[0][0]);
    glUniform1f(glGetUniformLocation(program, "u_time"), ctx.delta);
    // ...

    // Draw!
    glBindVertexArray(meshVAO.vao);
    glDrawArraysInstancedARB(GL_TRIANGLE_STRIP, 0, 4, ParticlesCount);
    glBindVertexArray(ctx.defaultVAO);
}

void display(Context &ctx)
{
    glClearColor(0.2, 0.2, 0.2, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST); // ensures that polygons overlap correctly
    glDepthFunc(GL_LESS);
    drawMesh(ctx, ctx.program, ctx.meshVAO);
}

void reloadShaders(Context *ctx)
{
    glDeleteProgram(ctx->program);
    ctx->program = loadShaderProgram(shaderDir() + "mesh.vert",
                                     shaderDir() + "mesh.frag");
}

void mouseButtonPressed(Context *ctx, int button, int x, int y)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        ctx->trackball.center = glm::vec2(x, y);
        trackballStartTracking(ctx->trackball, glm::vec2(x, y));
    }
}

void mouseButtonReleased(Context *ctx, int button, int x, int y)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        trackballStopTracking(ctx->trackball);
    }
}

void moveTrackball(Context *ctx, int x, int y)
{
    if (ctx->trackball.tracking) {
        trackballMove(ctx->trackball, glm::vec2(x, y));
    }
}

void errorCallback(int error, const char* description)
{
    std::cerr << description << std::endl;
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
#ifdef WITH_TWEAKBAR
    if (TwEventKeyGLFW3(window, key, scancode, action, mods)) return;
#endif // WITH_TWEAKBAR

    Context *ctx = static_cast<Context *>(glfwGetWindowUserPointer(window));
    if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        reloadShaders(ctx);
    }
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
#ifdef WITH_TWEAKBAR
    if (TwEventMouseButtonGLFW3(window, button, action, mods)) return;
#endif // WITH_TWEAKBAR

    double x, y;
    glfwGetCursorPos(window, &x, &y);

    Context *ctx = static_cast<Context *>(glfwGetWindowUserPointer(window));
    if (action == GLFW_PRESS) {
        mouseButtonPressed(ctx, button, x, y);
    }
    else {
        mouseButtonReleased(ctx, button, x, y);
    }
}

void cursorPosCallback(GLFWwindow* window, double x, double y)
{
#ifdef WITH_TWEAKBAR
    // Apply screen-to-pixel scaling factor to support retina/high-DPI displays
    int framebuffer_width, window_width;
    glfwGetFramebufferSize(window, &framebuffer_width, nullptr);
    glfwGetWindowSize(window, &window_width, nullptr);
    float screen_to_pixel = float(framebuffer_width) / window_width;
    if (TwEventCursorPosGLFW3(window, (screen_to_pixel * x), (screen_to_pixel * y))) return;
#endif // WITH_TWEAKBAR

    Context *ctx = static_cast<Context *>(glfwGetWindowUserPointer(window));
    moveTrackball(ctx, x, y);
}

void resizeCallback(GLFWwindow* window, int width, int height)
{
#ifdef WITH_TWEAKBAR
    TwWindowSize(width, height);
#endif // WITH_TWEAKBAR

    Context *ctx = static_cast<Context *>(glfwGetWindowUserPointer(window));
    ctx->width = width;
    ctx->height = height;
    ctx->aspect = float(width) / float(height);
    ctx->trackball.radius = double(std::min(width, height)) / 2.0;
    ctx->trackball.center = glm::vec2(width, height) / 2.0f;
    glViewport(0, 0, width, height);
}

int main(void)
{
    Context ctx;

    // Create a GLFW window
    glfwSetErrorCallback(errorCallback);
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    ctx.width = 800;
    ctx.height = 800;
    ctx.aspect = float(ctx.width) / float(ctx.height);
    ctx.window = glfwCreateWindow(ctx.width, ctx.height, "Particle System", nullptr, nullptr);
    glfwMakeContextCurrent(ctx.window);
    glfwSetWindowUserPointer(ctx.window, &ctx);
    glfwSetKeyCallback(ctx.window, keyCallback);
    glfwSetMouseButtonCallback(ctx.window, mouseButtonCallback);
    glfwSetCursorPosCallback(ctx.window, cursorPosCallback);
    glfwSetFramebufferSizeCallback(ctx.window, resizeCallback);

    // Load OpenGL functions
    glewExperimental = true;
    GLenum status = glewInit();
    if (status != GLEW_OK) {
        std::cerr << "Error: " << glewGetErrorString(status) << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "OpenGL version: " << glGetString(GL_VERSION) << std::endl;

    // Initialize AntTweakBar (if enabled)
#ifdef WITH_TWEAKBAR
    TwInit(TW_OPENGL_CORE, nullptr);
    // Get actual framebuffer size for retina/high-DPI displays
    glfwGetFramebufferSize(ctx.window, &ctx.width, &ctx.height);
    TwWindowSize(ctx.width, ctx.height);
    TwBar *tweakbar = TwNewBar("Settings");
#endif // WITH_TWEAKBAR

    // Initialize rendering
    glGenVertexArrays(1, &ctx.defaultVAO);
    glBindVertexArray(ctx.defaultVAO);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    init(ctx);

    float lastTime = glfwGetTime();
    // Start rendering loop
    while (!glfwWindowShouldClose(ctx.window)) {
        glfwPollEvents();
        float currentTime = glfwGetTime();
        ctx.delta = currentTime - lastTime;
        lastTime = currentTime;
        display(ctx);
#ifdef WITH_TWEAKBAR
        TwDraw();
#endif // WITH_TWEAKBAR
        glfwSwapBuffers(ctx.window);
    }

    // Shutdown
#ifdef WITH_TWEAKBAR
    TwTerminate();
#endif // WITH_TWEAKBAR
    glfwDestroyWindow(ctx.window);
    glfwTerminate();
    std::exit(EXIT_SUCCESS);
}
