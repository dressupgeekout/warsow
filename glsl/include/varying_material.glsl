qf_varying vec4 v_TexCoord_FogCoord;
#ifdef NUM_LIGHTMAPS
qf_varying qf_lmvec01 v_LightmapTexCoord01;
#if NUM_LIGHTMAPS > 2 
qf_varying qf_lmvec23 v_LightmapTexCoord23;
#endif
#ifdef LIGHTMAP_ARRAYS
qf_flat_varying vec4 v_LightmapLayer0123;
#endif
#endif

#if defined(NUM_DLIGHTS) || defined(APPLY_SPECULAR)
qf_varying vec3 v_Position;
#endif

#if defined(APPLY_SPECULAR) || defined(APPLY_OFFSETMAPPING) || defined(APPLY_RELIEFMAPPING)
qf_varying vec3 v_EyeVector;
#endif

qf_varying mat3 v_StrMatrix; // directions of S/T/R texcoords (tangent, binormal, normal)
