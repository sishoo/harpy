#version 450

layout(push_constant) uniform pc { mat4 proj_mat, view_mat; };

layout(binding = 0) buffer l0
{
        uint indices[];
        float vertices[];

        uint nobjects;
        object_t objects[];

        atomic_uint ndraws;
        // VkDrawIndexedIndirectCommand draws[];
};

void main() { 
        gl_FragColor = vec4(1.0, 1.0, 1.0, 10.); 
}