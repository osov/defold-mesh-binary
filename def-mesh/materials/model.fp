#version 140

in highp vec4 var_position;
in highp vec3 var_normal;
in highp vec2 var_texcoord0;
in vec3 var_light;

uniform sampler2D tex_diffuse;

out vec4 frag_color;

void main()
{
    vec4 color = texture(tex_diffuse, var_texcoord0);

    vec3 ambient_light = vec3(0.2);
    vec3 diff_light = vec3(normalize(var_light - vec3(var_position.xy, 0.)));
    diff_light = max(dot(var_normal,diff_light), 0.0) + ambient_light;
    diff_light = clamp(diff_light, 0.0, 1.0);

    frag_color = vec4(color.rgb * diff_light, color.w);
}

