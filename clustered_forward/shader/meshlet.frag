#version 450


layout (binding = 0) buffer l0
{
        uint nvisible_meshlets;
        uint idx_visible_meshlets[];
};

void main()
{
        // if meshlet visible add it to new geometry buffer

        if (!meshlet_in_frustum(id))
                return;

        if (meshlet_is_backfacing(id))
                return;

        idx_visible_meshlets[atomicAdd(nvisible_meshlets, 1)] = gl_FragCoord.x + gl_FragCoord.y * ;  
}