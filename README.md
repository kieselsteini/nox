# Nox - a minimal 2D Lua game engine

## API

### nox.events

#### nox.events.emit(name [, ...])
Try to find a function with the *name* in the *nox.events* table and call it with the optional additional arguments. Returns *true* when the call was successful.
```lua
nox.events.emit('on_mouse_down', 1) -- fake a mouse down event
nox.events.emit('on_my_event', 'Foo!') -- call nox.events.ony_my_event('Foo!')
```

#### nox.events.on_key_down(key)
Called when a keyboard key was pressed.

#### nox.events.on_key_up(key)
Called when a keyboard key was released.

#### nox.events.on_text_input(text)
Called when a "text" was entered.

#### nox.events.on_mouse_down(button)
Called when a mouse button was pressed.

#### nox.events.on_mouse_up(button)
Called when a mouse button was released.

#### nox.events.on_mouse_moved(x, y)
Called when the mouse was moved.

#### nox.events.on_controller_added(id)
Called when a new game controller was added.

#### nox.events.on_controller_removed(id)
Called when a game controller as removed.

#### nox.events.on_controller_down(id, button)
Called when a button on a game controller was pressed.

#### nox.events.on_controller_up(id, button)
Called when a button on a game controller was released.

#### nox.events.on_controller_moved(id, axis, value)
Called when a stick on a game controller was moved.

### nox.video

#### nox.video.get_blend_mode
Return the current blending mode

Following modes are supported:
- none
- blend
- add
- mod

```lua
local mode = nox.video.get_blend_mode()
```

#### nox.video.set_blend_mode(mode)
Set the current blending mode.

Following modes are supported:
- none
- blend
- add
- mod

```lua
nox.video.set_blend_mode('add') -- set additive blending
```

#### nox.video.get_draw_color()
Return the current drawing color.
```lua
local r, g, b, a = nox.video.get_draw_color()
```

#### nox.video.set_draw_color(r, g, b [, a])
Set the current drawing color.
```lua
nox.video.set_draw_color(0xff, 0xff, 0xff) -- set color #ffffff
```

#### nox.video.get_render_target()
Return the current rendering target. You can render to images if you wish. This function will return *nil* if the window is the current render target.
```lua
local target = nox.video.get_render_target()
if target then
  print('Target is image', target:get_size())
else
  print('Target is the window')
end
```

#### nox.video.set_render_target(target)
Set the new render target. It can be *nil* if you want to render directly to the window or an image object.

```lua
nox.video.set_render_target(nil) -- render directly to the window
nox.video.set_render_target(image) -- render to an image object
```

#### nox.video.clear()
Clear the current render target.

#### nox.video.destroy_image(image)
Destroys an image object.
```lua
nox.video.destroy_image(image) -- explicitly destroy this image
image:destroy() -- alternative call
```

#### nox.video.load_image(binary_string)
Load from the given *binary_string* an image object.
```lua
local image = assert(nox.video.load_image(bytes)) -- load an image object
```

#### nox.video.create_image(width, height)
Create a new empty image with the given *width* and *height*. This image can be used as a render target.
```lua
local image = nox.video.create_image(320, 240) -- create a new empty image
```

#### nox.video.create_child_image(image, x, y, width, height)
Create a sub image from the given *image* object. This child image will share the same resources.
```lua
local child = nox.video.create_child_image(image, 0, 0, 8, 8) -- create a sub image
child = image:create_child(0, 0, 8, 8) -- alternative usage
```

#### nox.video.is_image_valid(image)
Returns *true* if the given *image* object is valid and not destroyed.
```lua
local valid = nox.video.is_image_valid(image)
local valid = image:is_valid()
```

#### nox.video.is_child_image(image)
Returns *true* if the given *image* object is a child image object.
```lua
local is_child = nox.video.is_child_image(image)
local is_child = image:is_child()
```

#### nox.video.get_image_size(image)
Return the width and the height of the given *image* object.
```lua
local width, height = nox.video.get_image_size(image)
local width, height = image:get_size()
```

#### nox.video.draw_image(image, x, y)
Draw the given *image* object to the render target.
```lua
nox.video.draw_image(image, 100, 100)
image:draw(100, 100)
```


### nox.window

#### nox.window.close()
Closes the window and eventually stop the event loop.
```lua
nox.window.close() -- stop the game
```

#### nox.window.get_title()
Return the current window title.
```lua
local title = nox.window.get_title()
```

#### nox.window.set_title(title)
Set the title for the window.
```lua
nox.window.set_title('My Game!')
```

#### nox.window.is_fullscreen()
Return *true* when the window is in fullscreen mode.
```lua
local is_fullscreen = nox.window.is_fullscreen()
```

#### nox.window.set_fullscreen(fullscreen)
Set the fullscreen mode for the window.
```lua
nox.window.set_fullscreen(true) -- go to fullscreen mode
```
