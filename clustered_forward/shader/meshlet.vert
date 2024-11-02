#version 450

layout (binding = 0) buffer l0
{

};

void main()
{
        uint sz = sqrt(2 * objects[gl_InstanceId].nmeshlets);
        uvec4 pos[] = (uvec4[]) 
        {
                uvec4(0, 0, 0, 1),
                uvec4(sz, 0, 0, 1),
                uvec4(0, sz, 0, 1)
        };

        gl_Position = pos[gl_VertexId];
}