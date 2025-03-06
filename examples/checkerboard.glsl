#version 450
#extension GL_EXT_shader_image_load_formatted : require

layout(set = 0, binding = 0)
uniform image2D renderTarget;

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;

void mainImage(out vec4 fragColor, vec2 fragCoord, vec2 iResolution)
{
    float cx = 2.0*fragCoord.x/iResolution.y-2.0;
    float cy = 2.0*fragCoord.y/iResolution.y-1.0;

    float zx = 0.0;
    float zy = 0.0;

    int iteration = 0;
    int max_iteration = 1000;

    // f(z) = z^2 + c, z and c are complex number.
    while ((zx*zx + zy*zy) <= 256.0 && iteration < max_iteration)
    {
        float next_zx = zx*zx - zy*zy + cx;
        zy = 2.0*zx*zy + cy;

        zx = next_zx;

        iteration++;
    }

    if (iteration == max_iteration)
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
    else
    {
        float smooth_iteration = float(iteration);

        // Smooth iteration use https://iquilezles.org/articles/msetsmooth/
        // |Zn| = zx*zx + zy*zy, B = 256.0, d = 2.0
        //smooth_iteration -= log(log(zx*zx + zy*zy)/log(256.0))/log(2.0);
        smooth_iteration -= log2(log2(zx*zx + zy*zy)) - 3.0; // Simplified with log2.

        fragColor = vec4(smooth_iteration/25.0, (smooth_iteration-25.0)/25.0, (smooth_iteration-50.0)/25.0, 1.0);
    }
}

void main() {
    ivec2 img_size = imageSize(renderTarget);
    if (gl_GlobalInvocationID.x >= img_size.x || gl_GlobalInvocationID.y >= img_size.y)
        return;

    uint ok = (gl_GlobalInvocationID.x + gl_GlobalInvocationID.y) % 2;
    vec4 c = (ok == 0) ? vec4(0.0) : vec4(1.0, 0.0, 1.0, 1.0);
    mainImage(c, vec2(gl_GlobalInvocationID.xy), vec2(img_size));
    if (ok == 0)
        imageStore(renderTarget, ivec2(gl_GlobalInvocationID.xy), c);
}