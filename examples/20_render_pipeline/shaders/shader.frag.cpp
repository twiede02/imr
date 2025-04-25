#include <stdint.h>
#include <shady.h>

#include "math.h"

using namespace vcc;
using namespace nasl;

#include "noise.h"

location(0) input native_vec3 fragColor;
location(1) input native_vec2 fragUV;

location(0) output native_vec4 outColor;

native_vec3 clamp(native_vec3, native_vec3, native_vec3) __asm__("shady::pure_op::GLSL.std.450::43::Invocation");
//vec3 clamp(vec3 x, vec3, vec3) { return x; }

extern "C" {

fragment_shader void main() {
    vec3 color = fragColor;

    float f = perlin_noise(fragUV);
    float off = 0.01f;
    float fx = perlin_noise(fragUV + vec2(0.0f, off));
    float fy = perlin_noise(fragUV + vec2(off, 0.0f));
    float dx = (fx - f);
    float dy = (fy - f);

    vec2 slope = vec2(dx, dy);
    //vec2 h = vec2(vcc::sqrtf(dx * dx + off * off), vcc::sqrtf(dy * dy + off * off));
    //vec3 normal = vec3(vec2(off) / h, 0.0);

    vec2 size = vec2(off, 0);
    vec3 va = normalize(vec3(size.xy,slope.x));
    vec3 vb = normalize(vec3(size.yx,slope.y));
    //vec4 bump = vec4( cross(va,vb), s11 );
    vec3 normal = normalize(cross(va, vb));
    //vec3 normal = clamp(vec3(dx, dy, 0.0) * 10.0, vec3(-100.0f), vec3(100.0f));
    //normal.z = vcc::sqrtf(1.0 - dot(normal.xy, normal.xy));
    //normal = normalize(normal);
    //color = color * ((0.5f - 0.5f * f));
    //color = 0.5 + 0.5 * normal;
    vec3 light = normalize(vec3(0.5f, 1.0f, 0.25f));
    vec3 diffuse = mix(vec3(0.1f, 0.9f, 0.3f), vec3(1.0f), clamp(powf(1.0f - f, 3.0f), 0.0f, 1.0f));
    color = max(dot(light, normal), vec3(0.0f, 0.0f, 0.0f)) * diffuse + vec3(0.2f) * diffuse;
    //color = vec3(1.0);

    float depth = gl_FragCoord.z;
    vec3 fog = vec3(0.8f, 0.9f, 1.0f);
    //float fog_dropoff = clamp(pow(depth - 0.2, 6) + 0.4, 0, 1);
    float fog_dropoff = clamp(smoothstep(0.95, 1.0, depth), 0, 1);

    color = mix(color, fog, fog_dropoff);
    //color = vec3(fog_dropoff);

    outColor = vec4(color, 1.0f);
    //outColor = vec4(normal * 0.5f + vec3(0.5f), 1.0f);
}

}
