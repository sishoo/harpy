#version 450

layout(push_constant) uniform pc { mat4 proj_mat, view_mat; };

layout(binding = 0) buffer l0
{
        uint indices[];
        float vertices[];

        uint nobjects;
        object_t objects;

        atomic_uint ndraws;
        VkDrawIndexedIndirectCommand draws[];
};

void main()
{
        uint id = gl_VertexId / 256; // 256 = number of verts in a meshlet
        uint offset = gl_VertexId % 256;

        uint idx = indices[id + offset];
        vec4 pos = vec4(
                vertices[idx],
                vertices[idx + 1],
                vertices[idx + 2],
                0
        );

        gl_Position = 
                proj_mat * view_mat * objects[gl_InstanceId].model_mat * pos;
}