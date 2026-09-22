# The Texture editor

Double-click a texture in the asset browser (or `OpenResourceEditor -Asset <guid>`): the bitmap in a 2D or 3D preview, the compiled bitmap's
information, the rendering options, and the descriptor. Everything the window changes goes through commands on the editor's own undo system.

```
source/Editor/xtexture_editor.h           the editor (session), its commands, its registration
source/Editor/xtexture_editor_preview.h   the preview (2D image, cube, sphere and so on) and its settings
```

The document, `SetProperty`, `Save`, `Compile`, `Undo` and `Redo` are the shared descriptor ones (`source/Tools/Editor/xeditor_descriptor_editor.h` in
the xGPU tree); this depot adds the texture specific commands and the preview.

## Commands

Run as `<resource name>\<Command>` (see `list`) or `ResourceEditorCommand -Asset <guid> -Cmd <base64>`. Paths and values are base64.

| Command | |
|---|---|
| `ListProperties [-Filter text]` | every descriptor property with its value: the paths `SetProperty` takes, e.g. `Texture/Mipmaps/GenerateMips` |
| `SetProperty -Path -Value [-Before]`, `SnapshotEdit` | any descriptor property (undoable); an enum takes its item name |
| `ListOp -Path -Op Insert\|Delete\|Move -Index n [-ToIndex n]` | inserts, deletes or moves an element in the middle of a 1D array property (undoable); ordinal keys only |
| `SetSRGB -Value`, `SetGenerateMips -Value` | the two properties the window has shortcuts for (undoable) |
| `Save`, `Compile` (also `SaveTexture`, `CompileTexture`) | save the descriptor; validate, save and queue the compile |
| `Undo`, `Redo` (also `UndoTexture`, `RedoTexture`) | |
| `CompileStatus [-Lines n]` | how the last compile went: state, unsaved changes, validation errors, the end of the log |
| `ListPreview [-Filter]`, `SetPreview -Path -Value` | the preview controls and options (render mode, channels, mip, gamma, ...), by the paths `ListPreview` prints |
| `SetCamera [-Yaw -Pitch -Distance]`, `GetCamera`, `FrameSubject` | the 3D preview camera (degrees) |
| `SetView -View image [-Zoom -PanX -PanY -Reset]` | zoom and pan of the 2D preview |

Only the descriptor is edited: the preview reads the compiled texture, so a change shows after the compile.
