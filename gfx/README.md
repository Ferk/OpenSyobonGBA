# Graphics Assets

Adapted source PNG assets live here. The root `Makefile` automatically converts
`gfx/*.png` files into C source/header pairs under `build/` using `grit`.

Default rule:

```sh
grit gfx/name.png -ftc -fh -gB4 -gTFF00FF -m! -p -obuild/name
```

Use magenta (`#ff00ff`) for transparent pixels in 4-bit sprite or tile assets.

Run `make prepare-assets` to rebuild the current adapted sheets from the
read-only OpenSyobonAction reference artwork:

- `tiles_16.png`: 16x16 background metatiles.
- `player_16.png`: 16x16 player animation frames.
- `traps_16.png`: trap block and projectile sprites.
