#!/usr/bin/env python3
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
REF = ROOT / "reference" / "OpenSyobonAction" / "res"
GFX = ROOT / "gfx"

MAGENTA = (255, 0, 255, 255)


def rgba(path):
    return Image.open(path).convert("RGBA")


def transparent_black(img):
    out = img.copy()
    pixels = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = pixels[x, y]
            if a == 0 or (r < 8 and g < 8 and b < 8):
                pixels[x, y] = MAGENTA
    return out


def transparent_ref_sprite(img):
    out = img.copy()
    pixels = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = pixels[x, y]
            if a == 0 or (r == 153 and g == 255 and b == 255):
                pixels[x, y] = MAGENTA
    return out


def crop_resize(img, box, size):
    return img.crop(box).resize(size, Image.Resampling.NEAREST)


def crop_resize_on_sky(img, box, size, sky):
    tile = crop_resize(img, box, size).convert("RGBA")
    out = Image.new("RGBA", size, sky + (255,))
    out.alpha_composite(transparent_black(tile))
    return out


def decor_tiles(img, box, cols, rows, sky):
    piece_w = (box[2] - box[0]) / cols
    piece_h = (box[3] - box[1]) / rows
    tiles = []

    for row in range(rows):
        for col in range(cols):
            piece_box = (
                int(box[0] + col * piece_w),
                int(box[1] + row * piece_h),
                int(box[0] + (col + 1) * piece_w),
                int(box[1] + (row + 1) * piece_h),
            )
            tiles.append(crop_resize_on_sky(img, piece_box, (16, 16), sky))

    return tiles


def transparent_decor_tiles(img, box, cols, rows):
    piece_w = (box[2] - box[0]) / cols
    piece_h = (box[3] - box[1]) / rows
    tiles = []

    for row in range(rows):
        for col in range(cols):
            piece_box = (
                int(box[0] + col * piece_w),
                int(box[1] + row * piece_h),
                int(box[0] + (col + 1) * piece_w),
                int(box[1] + (row + 1) * piece_h),
            )
            tiles.append(transparent_ref_sprite(
                crop_resize(img, piece_box, (16, 16))))

    return tiles


def checkpoint_tiles(img):
    checkpoint = transparent_ref_sprite(crop_resize(img, (40, 182, 80, 242), (32, 32)))
    tiles = [
        checkpoint.crop((col * 16, row * 16, col * 16 + 16, row * 16 + 16))
        for row in range(2)
        for col in range(2)
    ]
    tiles.extend([Image.new("RGBA", (16, 16), MAGENTA) for _ in range(2)])
    return tiles


def cloud_face_overlay_tiles(img):
    face_box = (151, 72, 221, 112)
    face = img.crop(face_box).convert("RGBA")
    face_px = face.load()
    black_pixels = {
        (x, y)
        for y in range(face.height)
        for x in range(face.width)
        if face_px[x, y][3] != 0 and
        face_px[x, y][0] < 32 and
        face_px[x, y][1] < 32 and
        face_px[x, y][2] < 32
    }
    visited = set()
    overlay = Image.new("RGBA", face.size, MAGENTA)
    overlay_px = overlay.load()

    for pixel in black_pixels:
        if pixel in visited:
            continue

        stack = [pixel]
        visited.add(pixel)
        component = []

        while stack:
            x, y = stack.pop()
            component.append((x, y))

            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if dx == 0 and dy == 0:
                        continue

                    neighbor = (x + dx, y + dy)
                    if neighbor in black_pixels and neighbor not in visited:
                        visited.add(neighbor)
                        stack.append(neighbor)

        touches_edge = any(
            x == 0 or y == 0 or x == face.width - 1 or y == face.height - 1
            for x, y in component
        )

        if not touches_edge:
            for x, y in component:
                overlay_px[x, y] = face_px[x, y]

    tiles = []

    for row in range(2):
        for col in range(4):
            tiles.append(crop_resize(
                overlay,
                (
                    int(col * overlay.width / 4),
                    int(row * overlay.height / 2),
                    int((col + 1) * overlay.width / 4),
                    int((row + 1) * overlay.height / 2),
                ),
                (16, 16),
            ))

    return tiles


def pipe_tile(part):
    img = Image.new("RGBA", (16, 16), (0, 230, 0, 255))
    px = img.load()

    for x in range(16):
        px[x, 0] = (0, 0, 0, 255)

    for y in range(16):
        if part.endswith("left"):
            px[0, y] = (0, 0, 0, 255)
            px[15, y] = (0, 150, 0, 255)
        else:
            px[0, y] = (0, 255, 80, 255)
            px[15, y] = (0, 0, 0, 255)

    if part.startswith("top"):
        for x in range(16):
            px[x, 15] = (0, 0, 0, 255)
        if part.endswith("left"):
            for y in range(2, 14):
                px[14, y] = (0, 150, 0, 255)
        else:
            for y in range(2, 14):
                px[1, y] = (0, 255, 80, 255)

    return img


def spike_tile():
    img = Image.new("RGBA", (16, 16), (168, 216, 255, 255))
    px = img.load()

    for spike in range(4):
        start = spike * 4
        for y in range(4, 16):
            half_width = (15 - y) // 2
            center = start + 2
            for x in range(center - half_width, center + half_width + 1):
                if 0 <= x < 16:
                    px[x, y] = (0, 230, 0, 255)
        px[start + 2, 3] = (0, 230, 0, 255)

    for x in range(16):
        px[x, 15] = (0, 0, 0, 255)

    return img


def goal_tiles(sky):
    out = Image.new("RGBA", (16, 64), sky + (255,))
    px = out.load()

    for y in range(0, 64):
        for x in range(5, 11):
            px[x, y] = (255, 255, 255, 255)
        px[4, y] = (0, 0, 0, 255)
        px[11, y] = (0, 0, 0, 255)

    cx = 8
    cy = 5
    r2 = 25
    for y in range(cy - 5, cy + 6):
        for x in range(cx - 5, cx + 6):
            dist = (x - cx) * (x - cx) + (y - cy) * (y - cy)
            if dist <= r2:
                px[x, y] = (248, 220, 80, 255)
            if r2 < dist <= 36:
                px[x, y] = (0, 0, 0, 255)

    return [
        out.crop((0, row * 16, 16, row * 16 + 16))
        for row in range(4)
    ]


def sheet_vertical(images):
    w = max(img.width for img in images)
    h = sum(img.height for img in images)
    out = Image.new("RGBA", (w, h), MAGENTA)
    y = 0
    for img in images:
        out.alpha_composite(img, (0, y))
        y += img.height
    return out


def quantize_to_palette(img, palette):
    indexed = Image.new("P", img.size)
    flat = []
    for r, g, b in palette:
        flat.extend([r, g, b])
    flat.extend([0, 0, 0] * (256 - len(palette)))
    indexed.putpalette(flat)

    px_in = img.convert("RGBA").load()
    px_out = indexed.load()
    lookup = {color: i for i, color in enumerate(palette)}
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px_in[x, y]
            if a == 0:
                key = (255, 0, 255)
            else:
                key = (r, g, b)
            px_out[x, y] = lookup.get(key, nearest_color((r, g, b), palette))
    return indexed


def nearest_color(color, palette):
    cr, cg, cb = color
    best_i = 0
    best_d = 1 << 30
    for i, (r, g, b) in enumerate(palette):
        d = (cr - r) * (cr - r) + (cg - g) * (cg - g) + (cb - b) * (cb - b)
        if d < best_d:
            best_i = i
            best_d = d
    return best_i


def save_png(name, image, palette):
    indexed = quantize_to_palette(image, palette)
    indexed.save(GFX / name)


def save_existing_player_png(name):
    path = GFX / name
    if not path.exists():
        return False

    img = Image.open(path).convert("RGBA")
    colors = []

    for _, color in img.getcolors(maxcolors=256) or []:
        r, g, b, a = color
        if a == 0 or (r >= 240 and g <= 16 and b >= 240):
            continue
        rgb = (r, g, b)
        if rgb not in colors:
            colors.append(rgb)

    palette = [(255, 0, 255)] + colors[:15]
    palette.extend([(0, 0, 0)] * (16 - len(palette)))
    indexed = quantize_to_palette(img, palette)
    indexed.save(path)
    return True


def sheet_grid(images, cols, cell_size=(16, 16)):
    rows = (len(images) + cols - 1) // cols
    out = Image.new("RGBA", (cols * cell_size[0], rows * cell_size[1]), MAGENTA)

    for i, img in enumerate(images):
        x = (i % cols) * cell_size[0]
        y = (i // cols) * cell_size[1]
        out.alpha_composite(img.convert("RGBA"), (x, y))

    return out


def tile_shape_signature(img):
    px = img.convert("RGBA").load()
    signature = []

    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            if a == 0 or (r >= 240 and g <= 16 and b >= 240):
                signature.append(0)
            elif r < 24 and g < 24 and b < 24:
                signature.append(2)
            else:
                signature.append(1)

    return tuple(signature)


def append_unique_tile(images, signatures, img):
    sig = tile_shape_signature(img)

    if sig in signatures:
        return False

    signatures.add(sig)
    images.append(img)
    return True


def extract_entry(name, source_name, source_img, box, size=(16, 16), transparent=False):
    img = source_img.crop(box)
    if transparent:
        img = transparent_ref_sprite(img)
    return {
        "name": name,
        "source": source_name,
        "box": box,
        "image": img.resize(size, Image.Resampling.NEAREST),
    }


def save_extract_sheet(name, entries, palette, cols=8):
    save_png(name, sheet_grid([entry["image"] for entry in entries], cols), palette)
    lines = []
    for i, entry in enumerate(entries):
        x = i % cols
        y = i // cols
        box = entry["box"]
        lines.append(
            f"{name} cell({x},{y}) index={i}: {entry['name']} "
            f"from {entry['source']} rect=({box[0]},{box[1]},{box[2] - box[0]},{box[3] - box[1]})"
        )
    return lines


def main():
    GFX.mkdir(exist_ok=True)

    bg_palette = [
        (255, 0, 255), (168, 216, 255), (152, 78, 36), (224, 154, 74),
        (248, 220, 80), (92, 168, 62), (60, 112, 38), (112, 72, 42),
        (72, 48, 28), (220, 220, 228), (152, 152, 164), (44, 44, 52),
        (255, 255, 255), (0, 230, 0), (0, 150, 0), (0, 0, 0),
    ]
    obj_palette = [
        (255, 0, 255), (255, 255, 255), (153, 153, 153), (0, 0, 0),
        (255, 0, 0), (255, 216, 64), (64, 128, 255), (80, 200, 80),
        (152, 78, 36), (224, 154, 74), (220, 220, 228), (44, 44, 52),
        (248, 80, 80), (120, 64, 180), (255, 160, 200), (0, 0, 0),
    ]

    brock = rgba(REF / "brock.PNG")
    brock2 = rgba(REF / "brock2.PNG")
    item = transparent_ref_sprite(rgba(REF / "item.PNG"))
    player = transparent_ref_sprite(rgba(REF / "player.PNG"))
    teki = transparent_ref_sprite(rgba(REF / "teki.PNG"))
    omake = transparent_ref_sprite(rgba(REF / "omake.PNG"))
    omake2 = transparent_ref_sprite(rgba(REF / "omake2.PNG"))
    haikei = rgba(REF / "haikei.PNG")
    sky = bg_palette[1]

    tile_sources = [
        Image.new("RGBA", (16, 16), sky + (255,)),
        crop_resize(brock, (33 * 1, 0, 33 * 1 + 30, 30), (16, 16)),
        crop_resize(brock, (33 * 2, 0, 33 * 2 + 30, 30), (16, 16)),
        crop_resize(brock, (33 * 4, 0, 33 * 4 + 30, 30), (16, 16)),
        crop_resize(brock, (33 * 5, 0, 33 * 5 + 30, 30), (16, 16)),
        crop_resize(brock, (33 * 6, 0, 33 * 6 + 30, 30), (16, 16)),
        crop_resize(brock, (0, 33, 30, 63), (16, 16)),
        crop_resize(brock2, (33 * 1, 0, 33 * 1 + 30, 30), (16, 16)),
    ]
    tile_sources.extend(decor_tiles(haikei, (0, 0, 150, 90), 5, 3, sky))
    tile_sources.extend(decor_tiles(haikei, (151, 31, 221, 71), 4, 2, sky))
    tile_sources.extend([
        pipe_tile("top_left"),
        pipe_tile("top_right"),
        pipe_tile("body_left"),
        pipe_tile("body_right"),
        spike_tile(),
    ])
    tile_sources.extend(decor_tiles(haikei, (151, 0, 216, 29), 4, 1, sky))
    tile_sources.extend(decor_tiles(haikei, (0, 91, 100, 181), 6, 3, sky))
    tile_sources.extend(checkpoint_tiles(haikei))
    tile_sources.extend(goal_tiles(sky))
    tile_signatures = {tile_shape_signature(tile) for tile in tile_sources}
    for row in range(0, 4):
        for col in range(0, 7):
            append_unique_tile(
                tile_sources,
                tile_signatures,
                crop_resize(brock,
                            (33 * col, 33 * row, 33 * col + 30, 33 * row + 30),
                            (16, 16)))
    for col in range(0, 7):
        if col == 1:
            continue
        append_unique_tile(tile_sources, tile_signatures,
                           crop_resize(brock2, (33 * col, 0, 33 * col + 30, 30),
                                       (16, 16)))
    for box in [(33, 33, 63, 63), (66, 33, 96, 63), (0, 66, 30, 96),
                (33, 66, 63, 96), (66, 66, 96, 96)]:
        append_unique_tile(tile_sources, tile_signatures,
                           crop_resize(brock2, box, (16, 16)))
    for tile in [
        crop_resize_on_sky(haikei, (151, 113, 202, 142), (16, 16), sky),
        crop_resize_on_sky(haikei, (222, 0, 250, 60), (16, 16), sky),
        *decor_tiles(haikei, (151, 143, 241, 183), 5, 2, sky),
    ]:
        append_unique_tile(tile_sources, tile_signatures, tile)
    if haikei.width >= 442:
        for tile in [
            *decor_tiles(haikei, (293, 0, 442, 90), 5, 3, sky),
            *decor_tiles(haikei, (293, 92, 357, 121), 4, 1, sky),
        ]:
            append_unique_tile(tile_sources, tile_signatures, tile)
    bg_sheet = sheet_vertical(tile_sources)
    save_png("tiles_16.png", bg_sheet, bg_palette)

    player_sources = [
        crop_resize(player, (31 * 4, 0, 31 * 4 + 30, 36), (16, 16)),
        crop_resize(player, (31 * 1, 0, 31 * 1 + 30, 36), (16, 16)),
        crop_resize(player, (31 * 2, 0, 31 * 2 + 30, 36), (16, 16)),
        crop_resize(player, (31 * 3, 0, 31 * 3 + 30, 36), (16, 16)),
    ]
    if not save_existing_player_png("player_16.png"):
        save_png("player_16.png", sheet_vertical(player_sources), obj_palette)

    trap_sources = [
        crop_resize(brock, (0, 0, 30, 30), (16, 16)),
        crop_resize(brock, (33 * 2, 0, 33 * 2 + 30, 30), (16, 16)),
        crop_resize(teki, (33 * 7 + 1, 0, 33 * 7 + 27, 30), (16, 16)),
    ]
    save_png("traps_16.png", sheet_vertical(trap_sources), obj_palette)

    item_sources = [
        crop_resize(item, (0, 0, 30, 30), (16, 16)),
        crop_resize(item, (33 * 1, 0, 33 * 1 + 30, 30), (16, 16)),
        crop_resize(item, (33 * 3, 0, 33 * 3 + 30, 30), (16, 16)),
        crop_resize(item, (33 * 4, 0, 33 * 4 + 30, 30), (16, 16)),
    ]
    save_png("items_16.png", sheet_vertical(item_sources), obj_palette)

    enemy_sources = [
        crop_resize(teki, (33 * 0, 0, 33 * 0 + 30, 30), (16, 16)),
        crop_resize(teki, (33 * 1, 0, 33 * 1 + 30, 43), (16, 16)),
        crop_resize(teki, (33 * 7 + 1, 0, 33 * 7 + 27, 30), (16, 16)),
        crop_resize(teki, (33 * 3, 0, 33 * 3 + 30, 44), (16, 16)),
    ]
    enemy_sources.extend(cloud_face_overlay_tiles(haikei))
    save_png("enemies_16.png", sheet_vertical(enemy_sources), obj_palette)

    sheet_map = []
    block_entries = []
    for row in range(4):
        for t in range(7):
            block_entries.append(extract_entry(
                f"grap[{t + row * 30}][1]", "brock.PNG", brock,
                (33 * t, 33 * row, 33 * t + 30, 33 * row + 30)))
    block_entries.extend([
        extract_entry("grap[8][1]", "brock.PNG", brock, (33 * 7, 0, 33 * 7 + 30, 30)),
        extract_entry("grap[16][1]", "item.PNG", item, (33 * 6, 0, 33 * 6 + 24, 27), transparent=True),
        extract_entry("grap[10][1]", "brock.PNG", brock, (33 * 9, 0, 33 * 9 + 30, 30)),
        extract_entry("grap[40][1]", "brock.PNG", brock, (33 * 9, 33, 33 * 9 + 30, 63)),
        extract_entry("grap[70][1]", "brock.PNG", brock, (33 * 9, 66, 33 * 9 + 30, 96)),
        extract_entry("grap[100][1]", "brock.PNG", brock, (33 * 9, 99, 33 * 9 + 30, 129)),
    ])
    for t in range(7):
        block_entries.append(extract_entry(
            f"grap[{t}][5]", "brock2.PNG", brock2,
            (33 * t, 0, 33 * t + 30, 30)))
    block_entries.extend([
        extract_entry("grap[10][5]", "brock2.PNG", brock2, (33, 33, 63, 63)),
        extract_entry("grap[11][5]", "brock2.PNG", brock2, (66, 33, 96, 63)),
        extract_entry("grap[12][5]", "brock2.PNG", brock2, (0, 66, 30, 96)),
        extract_entry("grap[13][5]", "brock2.PNG", brock2, (33, 66, 63, 96)),
        extract_entry("grap[14][5]", "brock2.PNG", brock2, (66, 66, 96, 96)),
        extract_entry("grap[0][5] extra", "omake.PNG", omake, (167, 0, 212, 45), transparent=True),
    ])
    sheet_map.extend(save_extract_sheet("source_blocks_16.png", block_entries, bg_palette))

    item_entries = []
    for t in range(6):
        item_entries.append(extract_entry(
            f"grap[{t}][2]", "item.PNG", item,
            (33 * t, 0, 33 * t + 30, 30), transparent=True))
    item_entries.extend([
        extract_entry("grap[100][3] good item", "item.PNG", item, (33, 0, 63, 30), transparent=True),
        extract_entry("grap[101][3] item", "item.PNG", item, (33 * 7, 0, 33 * 7 + 30, 30), transparent=True),
        extract_entry("grap[102][3] poison mushroom", "item.PNG", item, (33 * 3, 0, 33 * 3 + 30, 30), transparent=True),
        extract_entry("grap[105][3] question ball", "item.PNG", item, (33 * 5, 0, 33 * 5 + 30, 30), transparent=True),
        extract_entry("grap[110][3] bad star", "item.PNG", item, (33 * 4, 0, 33 * 4 + 30, 30), transparent=True),
    ])
    sheet_map.extend(save_extract_sheet("source_items_16.png", item_entries, obj_palette))

    trap_entries = [
        extract_entry("ttype<100 / visible block base grap[0][1]", "brock.PNG", brock, (0, 0, 30, 30)),
        extract_entry("ttype 112 / visible question grap[1][1]", "brock.PNG", brock, (33, 0, 63, 30)),
        extract_entry("ttype 100-103 / item block grap[2][1]", "brock.PNG", brock, (66, 0, 96, 30)),
        extract_entry("ttype 111/113 spent block grap[3][1]", "brock.PNG", brock, (99, 0, 129, 30)),
        extract_entry("ttype 7 invisible block grap[7][1]", "brock.PNG", brock, (231, 0, 261, 30)),
        extract_entry("ttype stagecolor row1 block grap[30][1]", "brock.PNG", brock, (0, 33, 30, 63)),
        extract_entry("ttype stagecolor row2 block grap[60][1]", "brock.PNG", brock, (0, 66, 30, 96)),
        extract_entry("ttype stagecolor row3 block grap[90][1]", "brock.PNG", brock, (0, 99, 30, 129)),
        extract_entry("moving poison mushroom atype102", "item.PNG", item, (99, 0, 129, 30), transparent=True),
        extract_entry("bad star atype110", "item.PNG", item, (132, 0, 162, 30), transparent=True),
        extract_entry("projectile/fire grap[9][3]", "teki.PNG", teki, (232, 0, 258, 30), transparent=True),
    ]
    sheet_map.extend(save_extract_sheet("source_traps_16.png", trap_entries, obj_palette))

    enemy_entries = [
        extract_entry("grap[0][3]", "teki.PNG", teki, (0, 0, 30, 30), transparent=True),
        extract_entry("grap[1][3]", "teki.PNG", teki, (33, 0, 63, 43), transparent=True),
        extract_entry("grap[2][3]", "teki.PNG", teki, (66, 0, 96, 30), transparent=True),
        extract_entry("grap[3][3]", "teki.PNG", teki, (99, 0, 129, 44), transparent=True),
        extract_entry("grap[4][3]", "teki.PNG", teki, (132, 0, 165, 35), transparent=True),
        extract_entry("grap[5][3]", "omake2.PNG", omake2, (0, 0, 37, 55), transparent=True),
        extract_entry("grap[6][3]", "omake2.PNG", omake2, (76, 0, 112, 50), transparent=True),
        extract_entry("grap[150][3]", "omake2.PNG", omake2, (150, 0, 186, 50), transparent=True),
        extract_entry("grap[7][3]", "teki.PNG", teki, (199, 0, 231, 32), transparent=True),
        extract_entry("grap[8][3]", "omake2.PNG", omake2, (187, 0, 224, 47), transparent=True),
        extract_entry("grap[151][3]", "omake2.PNG", omake2, (225, 0, 262, 47), transparent=True),
        extract_entry("grap[9][3]", "teki.PNG", teki, (232, 0, 258, 30), transparent=True),
        extract_entry("grap[10][3]", "omake.PNG", omake, (214, 0, 260, 16), transparent=True),
        extract_entry("grap[30][3]", "omake2.PNG", omake2, (0, 56, 30, 92), transparent=True),
        extract_entry("grap[155][3]", "omake2.PNG", omake2, (93, 56, 123, 92), transparent=True),
        extract_entry("grap[31][3]", "omake.PNG", omake, (50, 74, 99, 153), transparent=True),
        extract_entry("grap[80][3]", "haikei.PNG", haikei, (151, 31, 221, 71),
                      transparent=True),
        extract_entry("grap[81][3]", "haikei.PNG", haikei, (151, 72, 221, 112),
                      transparent=True),
        extract_entry("grap[130][3]", "haikei.PNG", haikei, (222, 72, 292, 112),
                      transparent=True),
        extract_entry("grap[82][3]", "brock2.PNG", brock2, (33, 0, 63, 30)),
        extract_entry("grap[83][3]", "omake.PNG", omake, (0, 0, 49, 48), transparent=True),
        extract_entry("grap[84][3]", "teki.PNG", teki, (166, 0, 196, 30), transparent=True),
        extract_entry("grap[86][3]", "omake.PNG", omake, (102, 66, 151, 125), transparent=True),
        extract_entry("grap[152][3]", "omake.PNG", omake, (152, 66, 201, 125), transparent=True),
        extract_entry("grap[90][3]", "omake.PNG", omake, (102, 0, 166, 63), transparent=True),
    ]
    sheet_map.extend(save_extract_sheet("source_enemies_16.png", enemy_entries, obj_palette))

    background_entries = [
        extract_entry("grap[0][4]", "haikei.PNG", haikei, (0, 0, 150, 90)),
        extract_entry("grap[1][4]", "haikei.PNG", haikei, (151, 0, 216, 29)),
        extract_entry("grap[2][4]", "haikei.PNG", haikei, (151, 31, 221, 71)),
        extract_entry("grap[3][4]", "haikei.PNG", haikei, (0, 91, 100, 181)),
        extract_entry("grap[4][4]", "haikei.PNG", haikei, (151, 113, 202, 142)),
        extract_entry("grap[5][4]", "haikei.PNG", haikei, (222, 0, 250, 60)),
        extract_entry("grap[6][4]", "haikei.PNG", haikei, (151, 143, 241, 183)),
        extract_entry("grap[20][4]", "haikei.PNG", haikei, (40, 182, 80, 242)),
    ]
    if haikei.width >= 442:
        background_entries.extend([
            extract_entry("grap[30][4]", "haikei.PNG", haikei, (293, 0, 442, 90)),
            extract_entry("grap[31][4]", "haikei.PNG", haikei, (293, 92, 357, 121)),
        ])
    sheet_map.extend(save_extract_sheet("source_background_16.png", background_entries, bg_palette))

    player_entries = [
        extract_entry("grap[0][0]", "player.PNG", player, (31 * 4, 0, 31 * 4 + 30, 36), transparent=True),
        extract_entry("grap[1][0]", "player.PNG", player, (31, 0, 61, 36), transparent=True),
        extract_entry("grap[2][0]", "player.PNG", player, (62, 0, 92, 36), transparent=True),
        extract_entry("grap[3][0]", "player.PNG", player, (93, 0, 123, 36), transparent=True),
        extract_entry("grap[40][0]", "syobon3.PNG", transparent_ref_sprite(rgba(REF / "syobon3.PNG")), (0, 0, 30, 36), transparent=True),
        extract_entry("grap[41][0]", "omake.PNG", omake, (50, 0, 101, 73), transparent=True),
    ]
    sheet_map.extend(save_extract_sheet("source_player_16.png", player_entries, obj_palette))

    (GFX / "asset_sheet_map.txt").write_text("\n".join(sheet_map) + "\n")


if __name__ == "__main__":
    main()
