# TexturePacker

`TexturePacker` is an Unreal Editor plugin for packing and unpacking texture channels directly from source asset data.

Developed and tested on Unreal Engine `5.7`.

<img width="628" height="579" alt="image" src="https://github.com/user-attachments/assets/a1331678-ccd6-4205-b623-7ef5b5902aef" />


## Features

- Content Browser right-click actions under `Texture Packer`
- Single `Pack` workflow:
  - `R + G + B -> RGB`
- Single `Unpack` workflow:
  - `RGB -> R, G, B`
- `Batch Pack` workflow for texture sets
- `Batch Unpack` workflow for multiple packed textures
- Direct source-data processing
- Validation before execution
- Undo support
- Progress notification with cancel support for batch actions

## Engine Version

Created, developed, and tested on Unreal Engine `5.7`.

If you use another engine version, some editor APIs may require adjustment.

## Installation

### 1. Copy plugin into project

Place the plugin here:

```text
YourProject/Plugins/TexturePacker
```

### 2. Enable plugin in `.uproject`

Add this to your project `.uproject`:

```json
{
	"Plugins": [
		{
			"Name": "TexturePacker",
			"Enabled": true,
			"TargetAllowList": [
				"Editor"
			]
		}
	]
}
```

### 3. Open project

Open the project in Unreal Editor `5.7`.

If needed, rebuild the project so the editor module is compiled.

## Usage

All actions are available from the Content Browser right-click menu:

```text
Texture Packer
```

<img width="523" height="215" alt="image" src="https://github.com/user-attachments/assets/b5e52b3e-a1ea-4a2b-9d11-e3edfdcf5923" />


## Pack

Use `Pack` when you want to combine three textures into one packed mask texture.

<img width="732" height="241" alt="image" src="https://github.com/user-attachments/assets/307548e7-8f50-41ae-8538-a11f8024bb83" />


### Requirements

- Exactly `3` selected `Texture2D` assets
- Each source texture should represent one channel:
  - `R`
  - `G`
  - `B`

### What it does

- Reads source texture data directly
- Packs source `R` channels into output `RGB`
- Creates a new texture asset
- Forces:
  - `sRGB = false`
  - `Compression = Masks`
- Lets you choose `Texture Group`

### Typical use case

- `Occlusion + Roughness + Metallic -> ORM`

<img width="628" height="579" alt="image" src="https://github.com/user-attachments/assets/a1331678-ccd6-4205-b623-7ef5b5902aef" />

## Unpack

Use `Unpack` when you want to split one packed texture into separate channel textures.

<img width="757" height="203" alt="image" src="https://github.com/user-attachments/assets/6469c5a6-9909-4496-b9b6-21af9966f550" />


### Requirements

- Exactly `1` selected `Texture2D` asset

### What it does

<img width="553" height="726" alt="image" src="https://github.com/user-attachments/assets/4cb50083-5b8e-46c8-8250-24394f2d5228" />

- Reads texture source data directly
- Extracts `R`, `G`, and `B` channels into new textures
- Lets you enable or disable each output channel
- Lets you set:
  - base name
  - suffixes
  - `Texture Group`
  - `Compression: Alpha`
- Forces:
  - `sRGB = false`

<img width="435" height="223" alt="image" src="https://github.com/user-attachments/assets/e7442196-05bc-4147-a632-69c19c6652b3" />

### Compression: Alpha

If enabled:

- output textures use alpha-style compression
- channel data is treated as single-channel red data

<img width="618" height="305" alt="image" src="https://github.com/user-attachments/assets/f1872f6c-f315-497a-842c-ac8a2d3bfc6c" />


## Batch Pack

Use `Batch Pack` when you have multiple texture sets that should be packed in one pass.

<img width="635" height="906" alt="image" src="https://github.com/user-attachments/assets/ab582c2c-753b-4a43-b462-e66a6363ea35" />


### Requirements

- Selected texture count must be divisible by `3`

### What it does

- Detects groups by shared base name and suffixes
- Lets you define detection suffixes for:
  - `R`
  - `G`
  - `B`
- Lets you apply suffix presets:
  - `Long`
  - `Short`
- Lets you manually fix channel assignment per group
- Lets you set output names per group
- Uses one shared `Texture Group`
- Creates packed textures with:
  - `sRGB = false`
  - `Compression = Masks`

### Typical naming patterns

Short:

- `_O`
- `_R`
- `_M`

Long:

- `_Occlusion`
- `_Roughness`
- `_Metallic`

## Batch Unpack

Use `Batch Unpack` when you want to split many packed textures at once.

<img width="555" height="653" alt="image" src="https://github.com/user-attachments/assets/fd244cad-3576-4e36-8bb4-7380e63fde0b" />


### Requirements

- At least `2` selected `Texture2D` assets

### What it does

- Uses one shared suffix setup for all selected textures
- Lets you apply suffix presets:
  - `Long`
  - `Short`
- Lets you set:
  - `Texture Group`
  - `Compression: Alpha`
- Creates unpacked textures in the same folder as each source texture

## Validation

The plugin validates inputs before execution.

Examples:

- wrong selected asset count
- missing channel textures
- duplicate textures assigned to multiple channels
- empty output names
- invalid source texture data
- invalid channel layout
- duplicate output names
- existing asset conflicts

## Undo

Single and batch create operations use editor transactions.

You can undo created assets with normal Unreal Editor undo behavior.

## Progress and Cancel

Batch actions use bottom-right progress notifications.

You can:

- monitor progress
- cancel remaining work
- open result details after completion

Already created assets are kept if the operation is canceled midway.

## Output Rules

### Pack output

- New texture asset
- Same folder as source `R` texture
- `sRGB = false`
- `Compression = Masks`

### Unpack output

- New texture assets
- Same folder as source texture
- `sRGB = false`
- `Texture Group` from input selection
- Optional `Compression: Alpha`

## Reviews

Recommended review points before production use:

- verify naming conventions used in your project
- verify compression settings on generated textures
- verify texture group selection
- verify batch grouping rules on your asset set
- test on duplicated or backup assets first

## Notes

- This is an editor-only plugin
- It is intended for Unreal Engine `5.7`
- Some operations modify or create source assets directly
- Batch workflows are designed for large content sets but should still be reviewed before running on production libraries

## License

[MIT](LICENSE)
