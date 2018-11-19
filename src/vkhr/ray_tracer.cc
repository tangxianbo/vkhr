#include <vkhr/ray_tracer.hh>

#include <utility>
#include <iostream>
#include <string>

#include <xmmintrin.h>
#include <pmmintrin.h>

#include <limits>
#include <vector>
#include <cmath>

namespace vkhr {
    static void embree_debug_callback(void*, const RTCError code,
                                             const char* message) {
        if (code == RTC_ERROR_UNKNOWN)
            return;
        if (message) {
            std::cerr << '\n'
                      << message
                      << std::endl;
        }
    }

    Raytracer::Raytracer(const Camera& camera, vkhr::HairStyle& hair_style) {
        set_flush_to_zero();
        set_denormal_zero();

        device = rtcNewDevice("verbose=1");

        rtcSetDeviceErrorFunction(device, embree_debug_callback, nullptr);

        scene = rtcNewScene(device);

        hair_vertices = hair_style.create_position_thickness_data();
        hair_style.generate_control_points_for(HairStyle::CurveType::Line);

        const auto& hair_indices = hair_style.get_control_points();

        hair = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_FLAT_LINEAR_CURVE);

        rtcSetSharedGeometryBuffer(hair, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT4,
                                   hair_vertices.data(), 0, sizeof(hair_vertices[0]),
                                   hair_vertices.size());
        rtcSetSharedGeometryBuffer(hair, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT,
                                   hair_indices.data(), 0, sizeof(hair_indices[0]),
                                   hair_indices.size());

        rtcCommitGeometry(hair);
        rtcAttachGeometry(scene, hair);
        rtcReleaseGeometry(hair);

        rtcCommitScene(scene);

        back_buffer = Image {
            camera.get_width(),
            camera.get_height()
        };

        back_buffer.clear();
    }

    void Raytracer::load(const SceneGraph&) {
    }

    void Raytracer::draw(const SceneGraph&) {
        // TODO: use the actual scene graphs!
    }

    void Raytracer::draw(const Camera& camera) {
        auto hair_color = glm::vec3(0.80f, 0.57f, 0.32f) * 0.40f;
        auto light = glm::normalize(glm::vec3(1.0f, 2.0f, 1.0f));
        auto light_color = glm::vec3(1.0f, 0.77f, 0.56f) * 0.20f;

        auto viewing_plane = camera.get_viewing_plane();

        back_buffer.clear();

        #pragma omp parallel for schedule(dynamic)
        for (int j = 0; j < static_cast<int>(back_buffer.get_height()); ++j)
        for (int i = 0; i < static_cast<int>(back_buffer.get_width()); ++i) {
            float x { static_cast<float>(i) }, y { static_cast<float>(j) };

            RTCIntersectContext      context;
            rtcInitIntersectContext(&context);

            auto eye_direction = glm::normalize(x * viewing_plane.x +
                                                y * viewing_plane.y +
                                                    viewing_plane.z);
            Ray ray { viewing_plane.point, eye_direction , 0.0000f };

            if (ray.intersects(scene, context)) {
                glm::vec4 color { hair_color * 0.5f, 1.0 };

                Ray shadow_ray { ray.get_intersection_point(), light, Ray::Epsilon };

                auto tangent = glm::vec4 { ray.get_tangent(), 0 };
                tangent = camera.get_view_matrix() * tangent;

                if (shadow_ray.occluded_by(scene, context) || shadows_off) {
                    auto shading = kajiya_kay(hair_color, light_color, 80.0f,
                                              glm::normalize(tangent), light,
                                              glm::vec3(0.00f, 0.0f, 0.00f));
                    if (shadows_off) color = glm::vec4 { shading, 1 };
                    else color += glm::vec4 { shading * 0.5f, 0.00f };
                }

                back_buffer.set_pixel(i, j, { std::clamp(color.r, 0.0f, 1.0f) * 255,
                                              std::clamp(color.g, 0.0f, 1.0f) * 255,
                                              std::clamp(color.b, 0.0f, 1.0f) * 255,
                                              std::clamp(color.a, 0.0f, 1.0f) * 255 });
            }
        }

        back_buffer.horizontal_flip();
        back_buffer.save("render.png");
    }

    Raytracer::~Raytracer() noexcept {
        rtcReleaseScene(scene);
        rtcReleaseDevice(device);
    }

    Raytracer::Raytracer(Raytracer&& raytracer) noexcept {
        swap(*this, raytracer);
    }

    Raytracer& Raytracer::operator=(Raytracer&& raytracer) noexcept {
        swap(*this, raytracer);
        return *this;
    }

    void swap(Raytracer& lhs, Raytracer& rhs) {
        using std::swap;
        std::swap(lhs.device, rhs.device);
    }

    void Raytracer::set_flush_to_zero() {
        _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    }

    void Raytracer::set_denormal_zero() {
        _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    }

    glm::vec3 Raytracer::kajiya_kay(const glm::vec3& diffuse,
                                    const glm::vec3& specular,
                                    float p,
                                    const glm::vec3& tangent,
                                    const glm::vec3& light,
                                    const glm::vec3& eye) {
        float cosTL = glm::dot(tangent, light);
        float cosTE = glm::dot(tangent, eye);

        float cosTL_squared = cosTL*cosTL;
        float cosTE_squared = cosTE*cosTE;

        float one_minus_cosTL_squared = 1.0f - cosTL_squared;
        float one_minus_cosTE_squared = 1.0f - cosTE_squared;

        float sinTL = one_minus_cosTL_squared / std::sqrt(one_minus_cosTL_squared);
        float sinTE = one_minus_cosTE_squared / std::sqrt(one_minus_cosTE_squared);

        glm::vec3 diffuse_colors = diffuse  * sinTL;
        glm::vec3 specular_color = specular * std::pow((cosTL * cosTE + sinTL * sinTE), p);

        return diffuse_colors + specular_color;
    }

    void Raytracer::toggle_shadows() {
        shadows_off = !shadows_off;
    }

    Ray::Ray(const glm::vec3& origin, const glm::vec3& direction, float tnear_plane) {
        ray_hit.hit.geomID = RTC_INVALID_GEOMETRY_ID;

        ray_hit.ray.org_x = origin.x;
        ray_hit.ray.org_y = origin.y;
        ray_hit.ray.org_z = origin.z;

        ray_hit.ray.dir_x = direction.x;
        ray_hit.ray.dir_y = direction.y;
        ray_hit.ray.dir_z = direction.z;

        ray_hit.ray.tnear = tnear_plane;
        ray_hit.ray.tfar  = std::numeric_limits<float>::infinity();
    }

    RTCRay& Ray::get_ray() {
        return ray_hit.ray;
    }

    RTCHit& Ray::get_hit() {
        return ray_hit.hit;
    }

    glm::vec3 Ray::get_origin() const {
        return { ray_hit.ray.org_x,
                 ray_hit.ray.org_y,
                 ray_hit.ray.org_z };
    }

    glm::vec3 Ray::get_direction() const {
        return { ray_hit.ray.dir_x,
                 ray_hit.ray.dir_y,
                 ray_hit.ray.dir_z };
    }

    bool Ray::hit_surface() const {
        return ray_hit.hit.geomID != RTC_INVALID_GEOMETRY_ID;
    }

    bool Ray::is_occluded() const {
        return ray_hit.ray.tfar < 0.0;
    }

    glm::vec2 Ray::get_uv() const {
        return { ray_hit.hit.u,
                 ray_hit.hit.v };
    }

    glm::vec3 Ray::get_normal() const {
        return { ray_hit.hit.Ng_x,
                 ray_hit.hit.Ng_y,
                 ray_hit.hit.Ng_z };
    }

    glm::vec3 Ray::get_tangent() const {
        return get_normal();
    }

    unsigned Ray::get_primitive_id() const {
        return ray_hit.hit.primID;
    }

    unsigned Ray::get_geometry_id() const {
        return ray_hit.hit.geomID;
    }

    glm::vec3 Ray::get_intersection_point() const {
        return get_origin() + get_direction() * ray_hit.ray.tfar;
    }

    bool Ray::intersects(RTCScene& scene,  RTCIntersectContext& context) {
        rtcIntersect1(scene, &context, &ray_hit);
        return hit_surface();
    }

    bool Ray::occluded_by(RTCScene& scene, RTCIntersectContext& context) {
        rtcOccluded1(scene, &context, &ray_hit.ray);
        return is_occluded();
    }
}
