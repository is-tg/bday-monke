#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include "monke_obj.h"
#include "hbday_wav.h"

#define FCOLOR(r, g, b, a) ((SDL_FColor) {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f})
#define DARK_GREY FCOLOR(95, 87, 79, 255)
#define PEACH FCOLOR(255, 157, 129, 255)
#define WHITE FCOLOR(255, 255, 255, 255)

#define MAX_CONFETTI 1000
#define CONFETTI_PER_CLICK 20
#define CONFETTI_SIZE 8.0f
#define GRAVITY 300.0f

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_AudioStream *stream = NULL;

static Uint8 *wav_data = NULL;
static Uint32 wav_data_len = 0;

typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    int v1, v2, v3;
} Face;

typedef struct {
    float x, y;
    float vx, vy;
    float life;
    SDL_FColor color;
    int active;
} ConfettiParticle;

typedef struct {
    Vec3 *vertices;
    Face *faces;
    int vertex_count;
    int face_count;
} Mesh;

typedef struct {
    float rotation_x, rotation_y;
    float scale;
    Vec3 translation;
} Transform;

static Mesh mesh = {0};
static Transform transform = {0.0f, 0.0f, 1.0f, {0.0f, 0.0f, -5.0f}};
static int mouse_down = 0;
static int last_mouse_x = 0, last_mouse_y = 0;
static ConfettiParticle confetti[MAX_CONFETTI];
static SDL_Texture *circle_texture = NULL;

static float confetti_spawn_timer = 0.0f;
#define CONFETTI_SPAWN_INTERVAL 0.5f

static Uint64 last_performance_counter = 0;
static Uint64 performance_frequency = 0;


static void clear(SDL_FColor color)
{
    SDL_SetRenderDrawColorFloat(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(renderer);
}

static SDL_Texture* create_circle_texture(int size) {
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, 
                                           SDL_TEXTUREACCESS_TARGET, size, size);
    if (!texture) return NULL;
    
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    
    SDL_Texture *old_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, texture);
    
    SDL_SetRenderDrawColorFloat(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    
    SDL_SetRenderDrawColorFloat(renderer, 1, 1, 1, 1);
    float center = size / 2.0f;
    float radius = center - 1;
    
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float dx = x - center;
            float dy = y - center;
            if (dx*dx + dy*dy <= radius*radius) {
                SDL_RenderPoint(renderer, x, y);
            }
        }
    }
    
    SDL_SetRenderTarget(renderer, old_target);
    return texture;
}

static float random_float(float min, float max) {
    return min + SDL_randf() * (max - min);
}

static SDL_FColor random_confetti_color() {
    SDL_FColor colors[] = {
        FCOLOR(255, 100, 100, 255),
        FCOLOR(100, 255, 100, 255),
        FCOLOR(100, 100, 255, 255),
        FCOLOR(255, 255, 100, 255),
        FCOLOR(255, 100, 255, 255),
        FCOLOR(100, 255, 255, 255),
        FCOLOR(255, 200, 100, 255),
        FCOLOR(200, 100, 255, 255),
    };
    return colors[SDL_rand(8)];
}

static void spawn_confetti(float x, float y) {
    for (int i = 0; i < CONFETTI_PER_CLICK; i++) {
        for (int j = 0; j < MAX_CONFETTI; j++) {
            if (!confetti[j].active) {
                confetti[j].x = x;
                confetti[j].y = y;
                confetti[j].vx = random_float(-200, 200);
                confetti[j].vy = random_float(-300, -100);
                confetti[j].life = 1.0f;
                confetti[j].color = random_confetti_color();
                confetti[j].active = 1;
                break;
            }
        }
    }
}

static void update_confetti(float delta_time, int screen_height) {
    for (int i = 0; i < MAX_CONFETTI; i++) {
        if (confetti[i].active) {
            confetti[i].x += confetti[i].vx * delta_time;
            confetti[i].y += confetti[i].vy * delta_time;
            
            confetti[i].vy += GRAVITY * delta_time;
            
            confetti[i].life -= delta_time * 0.8f;
            
            if (confetti[i].life <= 0 || confetti[i].y > screen_height + 50) {
                confetti[i].active = 0;
            }
        }
    }
}

static void render_confetti() {
    if (!circle_texture) return;
    
    int active_count = 0;
    for (int i = 0; i < MAX_CONFETTI; i++) {
        if (confetti[i].active) active_count++;
    }
    
    if (active_count == 0) return;
    
    SDL_Vertex *vertices = SDL_malloc(active_count * 6 * sizeof(SDL_Vertex));
    if (!vertices) return;
    
    int vertex_index = 0;
    
    for (int i = 0; i < MAX_CONFETTI; i++) {
        if (!confetti[i].active) continue;
        
        float x = confetti[i].x;
        float y = confetti[i].y;
        float size = CONFETTI_SIZE;
        
        SDL_FColor color = confetti[i].color;
        color.a = confetti[i].life;
        
        vertices[vertex_index++] = (SDL_Vertex){
            .position = {x - size/2, y - size/2},
            .color = color,
            .tex_coord = {0, 0}
        };
        vertices[vertex_index++] = (SDL_Vertex){
            .position = {x + size/2, y - size/2},
            .color = color,
            .tex_coord = {1, 0}
        };
        vertices[vertex_index++] = (SDL_Vertex){
            .position = {x - size/2, y + size/2},
            .color = color,
            .tex_coord = {0, 1}
        };
        
        vertices[vertex_index++] = (SDL_Vertex){
            .position = {x + size/2, y - size/2},
            .color = color,
            .tex_coord = {1, 0}
        };
        vertices[vertex_index++] = (SDL_Vertex){
            .position = {x + size/2, y + size/2},
            .color = color,
            .tex_coord = {1, 1}
        };
        vertices[vertex_index++] = (SDL_Vertex){
            .position = {x - size/2, y + size/2},
            .color = color,
            .tex_coord = {0, 1}
        };
    }
    
    SDL_RenderGeometry(renderer, circle_texture, vertices, vertex_index, NULL, 0);
    
    SDL_free(vertices);
}

static Vec3 rotate_x(Vec3 v, float angle) {
    float cos_a = SDL_cosf(angle);
    float sin_a = SDL_sinf(angle);
    return (Vec3){
        v.x,
        v.y * cos_a - v.z * sin_a,
        v.y * sin_a + v.z * cos_a
    };
}

static Vec3 rotate_y(Vec3 v, float angle) {
    float cos_a = SDL_cosf(angle);
    float sin_a = SDL_sinf(angle);
    return (Vec3){
        v.x * cos_a + v.z * sin_a,
        v.y,
        -v.x * sin_a + v.z * cos_a
    };
}

static Vec3 project_3d_to_2d(Vec3 v, int screen_width, int screen_height) {
    float fov = 60.0f * SDL_PI_F / 180.0f;
    float aspect = (float)screen_width / screen_height;
    
    if (v.z > -0.1f) v.z = -0.1f;
    
    float projected_x = (v.x / (-v.z * SDL_tanf(fov / 2.0f))) * screen_width / 2.0f + screen_width / 2.0f;
    float projected_y = (-v.y / (-v.z * SDL_tanf(fov / 2.0f) / aspect)) * screen_height / 2.0f + screen_height / 2.0f;
    
    return (Vec3){projected_x, projected_y, v.z};
}

static int load_obj_embedded(Mesh *mesh) {
    // Create a memory buffer from the embedded data
    char *obj_data = (char*)assets_monke_obj;
    size_t data_len = assets_monke_obj_len;
    
    // First pass: count vertices and faces
    int vertex_count = 0, face_count = 0;
    char *line_start = obj_data;
    char *line_end;
    
    while (line_start < obj_data + data_len) {
        // Find end of current line
        line_end = line_start;
        while (line_end < obj_data + data_len && *line_end != '\n' && *line_end != '\0') {
            line_end++;
        }
        
        // Check line type
        if (line_end > line_start + 1) {
            if (line_start[0] == 'v' && line_start[1] == ' ') vertex_count++;
            else if (line_start[0] == 'f' && line_start[1] == ' ') face_count++;
        }
        
        // Move to next line
        line_start = line_end + 1;
    }
    
    // Allocate memory
    mesh->vertices = SDL_malloc(vertex_count * sizeof(Vec3));
    mesh->faces = SDL_malloc(face_count * sizeof(Face));
    mesh->vertex_count = vertex_count;
    mesh->face_count = face_count;
    
    if (!mesh->vertices || !mesh->faces) {
        SDL_Log("Failed to allocate memory for mesh");
        return 0;
    }
    
    // Second pass: parse data
    int v_index = 0, f_index = 0;
    line_start = obj_data;
    
    while (line_start < obj_data + data_len) {
        line_end = line_start;
        while (line_end < obj_data + data_len && *line_end != '\n' && *line_end != '\0') {
            line_end++;
        }
        
        // Null-terminate the line temporarily for parsing
        char saved_char = *line_end;
        *line_end = '\0';
        
        if (line_end > line_start + 1) {
            if (line_start[0] == 'v' && line_start[1] == ' ') {
                float x, y, z;
                if (SDL_sscanf(line_start, "v %f %f %f", &x, &y, &z) == 3) {
                    mesh->vertices[v_index++] = (Vec3){x, y, z};
                }
            }
            else if (line_start[0] == 'f' && line_start[1] == ' ') {
                int v1, v2, v3;
                char *token;
                char *line_copy = SDL_strdup(line_start);
                
                SDL_strtok_r(line_copy + 2, " \t\n", &token);
                if (token) {
                    v1 = SDL_atoi(token) - 1;
                    SDL_strtok_r(NULL, " \t\n", &token);
                    if (token) {
                        v2 = SDL_atoi(token) - 1;
                        SDL_strtok_r(NULL, " \t\n", &token);
                        if (token) {
                            v3 = SDL_atoi(token) - 1;
                            
                            if (v1 >= 0 && v1 < vertex_count && 
                                v2 >= 0 && v2 < vertex_count && 
                                v3 >= 0 && v3 < vertex_count) {
                                mesh->faces[f_index++] = (Face){v1, v2, v3};
                            }
                        }
                    }
                }
                SDL_free(line_copy);
            }
        }
        
        // Restore the character and move to next line
        *line_end = saved_char;
        line_start = line_end + 1;
    }
    
    SDL_Log("Loaded embedded OBJ: %d vertices, %d faces", vertex_count, f_index);
    mesh->face_count = f_index;
    return 1;
}

int load_embedded_wav(SDL_AudioSpec *spec, Uint8 **audio_buf, Uint32 *audio_len) {
    // Create an SDL_IOStream from the embedded data
    SDL_IOStream *io = SDL_IOFromConstMem(assets_hbday_wav, assets_hbday_wav_len);
    if (!io) {
        SDL_Log("Failed to create IO stream from embedded WAV data: %s", SDL_GetError());
        return 0;
    }
    
    // Load the WAV data using SDL_LoadWAV_IO
    if (!SDL_LoadWAV_IO(io, true, spec, audio_buf, audio_len)) {
        SDL_Log("Failed to load embedded WAV data: %s", SDL_GetError());
        return 0;
    }
    
    SDL_Log("Loaded embedded WAV: %d Hz, %d channels, %d bytes", 
            spec->freq, spec->channels, *audio_len);
    
    return 1;
}

static void render_mesh(Mesh *mesh, Transform *transform, int screen_width, int screen_height) {
    if (!mesh->vertices || !mesh->faces) return;
    
    Vec3 *projected = SDL_malloc(mesh->vertex_count * sizeof(Vec3));
    if (!projected) return;
    
    for (int i = 0; i < mesh->vertex_count; i++) {
        Vec3 v = mesh->vertices[i];
        
        v.x *= transform->scale;
        v.y *= transform->scale;
        v.z *= transform->scale;
        
        v = rotate_x(v, transform->rotation_x);
        v = rotate_y(v, transform->rotation_y);
        
        v.x += transform->translation.x;
        v.y += transform->translation.y;
        v.z += transform->translation.z;
        
        projected[i] = project_3d_to_2d(v, screen_width, screen_height);
    }
    
    SDL_SetRenderDrawColorFloat(renderer, WHITE.r, WHITE.g, WHITE.b, WHITE.a);
    
    for (int i = 0; i < mesh->face_count; i++) {
        Face f = mesh->faces[i];
        
        if (f.v1 >= 0 && f.v1 < mesh->vertex_count &&
            f.v2 >= 0 && f.v2 < mesh->vertex_count &&
            f.v3 >= 0 && f.v3 < mesh->vertex_count) {
            
            Vec3 p1 = projected[f.v1];
            Vec3 p2 = projected[f.v2];
            Vec3 p3 = projected[f.v3];
            
            if (p1.z < -0.1f && p2.z < -0.1f && p3.z < -0.1f) {
                SDL_RenderLine(renderer, p1.x, p1.y, p2.x, p2.y);
                SDL_RenderLine(renderer, p2.x, p2.y, p3.x, p3.y);
                SDL_RenderLine(renderer, p3.x, p3.y, p1.x, p1.y);
            }
        }
    }
    
    SDL_free(projected);
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    if (SDL_GetAudioStreamQueued(stream) < (int)wav_data_len) {
        SDL_PutAudioStreamData(stream, wav_data, wav_data_len);
    }

    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    
    Uint64 current_performance_counter = SDL_GetPerformanceCounter();
    float delta_time = (float)(current_performance_counter - last_performance_counter) / (float)performance_frequency;
    last_performance_counter = current_performance_counter;
    
    clear(DARK_GREY);

    if (!mouse_down) {
        transform.rotation_y += 0.5f * delta_time;
    }
    
    render_mesh(&mesh, &transform, w, h);

    confetti_spawn_timer += delta_time;
    if (confetti_spawn_timer >= CONFETTI_SPAWN_INTERVAL) {
        confetti_spawn_timer -= CONFETTI_SPAWN_INTERVAL; 
        spawn_confetti(random_float(0, w), random_float(0, h));
    }
    
    update_confetti(delta_time, h);
    render_confetti();
    
    SDL_RenderPresent(renderer);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    switch (event->type) {
        case SDL_EVENT_QUIT: {
            return SDL_APP_SUCCESS;
        }

        case SDL_EVENT_KEY_DOWN: {
            if (event->key.scancode == SDL_SCANCODE_ESCAPE) {
                return SDL_APP_SUCCESS;
            }
            else if (event->key.scancode == SDL_SCANCODE_EQUALS || 
                     event->key.scancode == SDL_SCANCODE_KP_PLUS) {
                transform.scale *= 1.1f;
            }
            else if (event->key.scancode == SDL_SCANCODE_MINUS || 
                     event->key.scancode == SDL_SCANCODE_KP_MINUS) {
                transform.scale *= 0.9f;
            }
            else if (event->key.scancode == SDL_SCANCODE_R) {
                transform.rotation_x = 0;
                transform.rotation_y = 0;
                transform.scale = 1.0f;
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event->button.button == SDL_BUTTON_LEFT) {
                spawn_confetti(event->button.x, event->button.y);
            } else if (event->button.button == SDL_BUTTON_RIGHT) {
                mouse_down = 1;
                last_mouse_x = event->button.x;
                last_mouse_y = event->button.y;
            }
            break;
        }

        case SDL_EVENT_FINGER_UP: {
            spawn_confetti(event->button.x, event->button.y);
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (event->button.button == SDL_BUTTON_RIGHT) {
                mouse_down = 0;
            }
            break;
        }

        case SDL_EVENT_MOUSE_MOTION: {
            if (mouse_down) {
                int dx = event->motion.x - last_mouse_x;
                int dy = event->motion.y - last_mouse_y;

                if (dx != 0 || dy != 0) {
                    transform.rotation_y += dx * 0.005f; 
                    transform.rotation_x += dy * 0.005f;

                    last_mouse_x = event->motion.x;
                    last_mouse_y = event->motion.y;
                }
            }
            break;
        }

        case SDL_EVENT_FINGER_MOTION: {
            int dx = event->tfinger.x - last_mouse_x;
            int dy = event->tfinger.y - last_mouse_y;

            if (dx != 0 || dy != 0) {
                transform.rotation_y += dx * 0.005f; 
                transform.rotation_x += dy * 0.005f;

                last_mouse_x = event->tfinger.x;
                last_mouse_y = event->tfinger.y;
            }
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL: {
            if (event->wheel.y > 0) {
                transform.scale *= 1.1f;
            } else if (event->wheel.y < 0) {
                transform.scale *= 0.9f;
            }
            break;
        }
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    SDL_AudioSpec spec;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    
    if (!SDL_CreateWindowAndRenderer("Birthday Monkey", 800, 600, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer() failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    
    performance_frequency = SDL_GetPerformanceFrequency();
    last_performance_counter = SDL_GetPerformanceCounter();

    memset(confetti, 0, sizeof(confetti));
    circle_texture = create_circle_texture(32);
    if (!circle_texture) {
        SDL_Log("Failed to create circle texture");
    }
    
    SDL_srand((unsigned int)SDL_GetTicks());
    
    if (!load_obj_embedded(&mesh)) {
        SDL_Log("Failed to load OBJ file");
    }

    if (!load_embedded_wav(&spec, &wav_data, &wav_data_len)) {
        SDL_Log("Couldn't load .wav file: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!stream) {
        SDL_Log("Couldn't create audio stream: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_ResumeAudioStreamDevice(stream);
    
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    if (circle_texture) {
        SDL_DestroyTexture(circle_texture);
    }
    
    if (mesh.vertices) SDL_free(mesh.vertices);
    if (mesh.faces) SDL_free(mesh.faces);
    
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_free(wav_data);
    SDL_Quit();
}
