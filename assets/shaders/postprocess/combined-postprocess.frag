#version 330

// The texture holding the scene pixels
uniform sampler2D tex;

// Settings from the application
uniform bool enable_vignette;
uniform bool enable_chromatic_aberration;
uniform float speed_factor; // 0.0 to 1.0 based on current speed vs max speed

// Read "assets/shaders/fullscreen.vert" to know what "tex_coord" holds;
in vec2 tex_coord;
out vec4 frag_color;

// Chromatic Aberration strength
#define CA_STRENGTH 0.0005

void main(){
    vec2 tc = tex_coord;

    // Fast path: if all effects are off, just pass through the pixel unchanged
    float effective_blur = max(0.0, speed_factor - 0.75) / 0.25;
    if (!enable_vignette && !enable_chromatic_aberration && effective_blur <= 0.0) {
        frag_color = texture(tex, tc);
        return;
    }

    vec4 color;

    // 1 & 2. Radial Speed Blur + Chromatic Aberration
    if (effective_blur > 0.0) {
        float blurAmount = effective_blur * 0.01;
        const int samples = 12;
        float invSamples = 1.0 / float(samples);
        
        vec4 blurColor = vec4(0.0);
        vec4 blurRed = vec4(0.0);
        vec4 blurBlue = vec4(0.0);

        for(int i = 0; i < samples; ++i) {
            float scale = 1.0 - blurAmount * (float(i) / float(samples - 1));
            vec2 samplePos = 0.5 + (tc - 0.5) * scale;
            
            blurColor += texture(tex, samplePos);
            
            if (enable_chromatic_aberration) {
                blurRed += texture(tex, samplePos - vec2(CA_STRENGTH, 0.0));
                blurBlue += texture(tex, samplePos + vec2(CA_STRENGTH, 0.0));
            }
        }
        
        color = blurColor * invSamples;
        if (enable_chromatic_aberration) {
            color.r = (blurRed * invSamples).r;
            color.b = (blurBlue * invSamples).b;
        }
    } else {
        color = texture(tex, tc);
        if (enable_chromatic_aberration) {
            color.r = texture(tex, tc - vec2(CA_STRENGTH, 0.0)).r;
            color.b = texture(tex, tc + vec2(CA_STRENGTH, 0.0)).b;
        }
    }

    // 3. Vignette
    if (enable_vignette) {
        vec2 ndc = tc * 2.0 - 1.0;
        float dist = length(ndc);
        float vignette = smoothstep(1.4, 0.6, dist);
        color.rgb *= mix(1.0, vignette, 0.65);
    }

    frag_color = color;
}
