# glad (generated)

Generated with glad 2.0.8 (`pip install glad2==2.0.8`). The generated code is
MIT-licensed (see THIRD_PARTY_NOTICES.md); the Khronos headers carry their own
Apache-2.0 / MIT notices in their file headers.

```sh
python -m glad --api gl:core=4.5 --extensions "" --out-path third_party/glad c --loader
python -m glad --api egl=1.5 \
  --extensions "EGL_EXT_platform_base,EGL_MESA_platform_surfaceless,EGL_KHR_surfaceless_context,EGL_KHR_create_context,EGL_EXT_device_base,EGL_EXT_device_enumeration,EGL_EXT_platform_device" \
  --out-path third_party/glad c --loader
```

Strata loads GL through `gladLoadGLUserPtr` with the surface's own
`GetProcAddress` (SDL3, EGL, or the host's), so it never links `libGL` and
never relies on `GL_GLEXT_PROTOTYPES`. The EGL loader opens `libEGL.so.1` at
run time, only when a headless surface is requested.
