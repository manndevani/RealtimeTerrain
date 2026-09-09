# Third-Party Content Notices

This file records content assets in this repository that originate from third-party projects,
per their license terms.

## UnrealTransformGizmo (gizmo handle meshes)

Source: https://github.com/robinwood3d/UnrealTransformGizmo
License: MIT
Copyright (c) 2023 Robin Wood

Files migrated into this project:
- `Content/Gizmo/Meshes/SM_RotationHandle.uasset`
- `Content/Gizmo/Meshes/SM_ScaleHandle.uasset`
- `Content/Gizmo/Meshes/SM_UniformScaleHandle.uasset`

Only these three static mesh assets were migrated (no materials, Blueprints, or code — the
upstream repository is a pure Blueprint content pack with no C++ source). They are used as the
rotation-ring and scale-handle geometry for this project's own C++ runtime transform gizmo system
(`Plugins/GRT/Source/GRT/Public/Gizmo/`); the upstream project's own materials,
Blueprint interaction logic, and hover/highlight system are not used — this project's existing
dynamic-material-instance coloring system is used instead.

Full license text, per the MIT License's requirement to include the copyright and permission
notice in all copies or substantial portions of the Software:

```
MIT License

Copyright (c) 2023 Robin Wood

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
