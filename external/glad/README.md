# glad (generated — do not edit)

OpenGL loader generated with [glad2](https://github.com/Dav1dde/glad) 2.0.8:

```bash
python3 -m venv /tmp/glad-venv && /tmp/glad-venv/bin/pip install glad2==2.0.8
/tmp/glad-venv/bin/glad --api gl:core=4.6 --extensions GL_KHR_debug --out-path external/glad c
```

- **Core profile 4.6** only, so deprecated/compat entry points (`glBegin`, `GL_QUADS`, ...) don't even
  compile. `Glint_gl` used a compatibility-profile loader; this one is deliberately stricter.
- `GL_KHR_debug` is core since 4.3 but listed explicitly so the `GLAD_GL_KHR_debug` flag exists.
- Usage: `gladLoadGL(glfwGetProcAddress)` after `glfwMakeContextCurrent`; include `<glad/gl.h>` before GLFW.
