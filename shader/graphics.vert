#version 450

layout (push_constant) uniform pc
{
        float dt;
        mat4 proj_mat, view_mat;
};

void main()
{       

        // object_t object = objects[gl_InstanceIndex];
        // gl_Position = proj_mat * view_mat * object.model_mat * vec4(object.pos, 1.0);

        vec3 verts[] = {
                vec3(1.0, -1.0, -1.0),
                vec3(1.0, -1.0, 1.0),
                vec3(-1.0, -1.0, 1.0),
                vec3(-1.0, -1.0, -1.0),
                vec3(1.0, 1.0, -1.0),
                vec3(1.0, 1.0, 1.0)
        };      

        gl_Position = proj_mat * view_mat * vec4(verts[gl_InstanceIndex], 1.0);
}