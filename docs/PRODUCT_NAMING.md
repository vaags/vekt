# Product Naming

Read this before naming a new product, scaffolding a plugin folder, or renaming an existing one.

## Vekt is the framework, not a product

Vekt is the name of the framework only. A product's name is its own word: **Rav**, not "Vekt Rav". This covers every
place the product name appears: the plugin's `PRODUCT_NAME`, its folder, CMake targets, the editor title, docs and
preset bank names. (Decided by Thomas, 3 October 2026.)

## The maker is Thomas Vaags

The maker is Thomas Vaags, an individual, not a company and not Vekt. Hosts show the maker next to the product
("Thomas Vaags › Rav"), so the maker-level fields must name him, not the framework: `COMPANY_NAME`,
`PLUGIN_MANUFACTURER_CODE`, the bundle-ID prefix and the user preset folder under `~/Library/Audio/Presets/`.
(Decided by Thomas, 3 October 2026.)

The existing products still use `COMPANY_NAME Vekt`, manufacturer code `Vekt`, `com.vekt.<product>` bundle IDs and
`~/Library/Audio/Presets/Vekt/`. The replacement manufacturer code and bundle-ID prefix are not chosen yet; ask Thomas
before setting them. All products must share one manufacturer code.

## Naming pattern

A product is named after a natural material in Norwegian:

1. **One short word**, one or two syllables, that is easy to say in English.
2. **A physical material** such as a stone, mineral, metal or resin, not an abstract idea or an adjective.
3. **A link to the sound.** Something about the material, such as its look, texture or behaviour, hints at what the
   product does.
4. **Plain ASCII.** Avoid æ, ø and å, which cause trouble in plugin IDs, bundle names, paths and search.
5. **Not already taken** by another audio product. Check before the name reaches a release.

| Product | Norwegian meaning | What it is | Link to the sound |
| --- | --- | --- | --- |
| Rav | amber | fossil resin, golden and translucent | warm, glowing colour, suits saturation and fuzz |
| Glimmer | mica | mineral in thin, glittering sheets | the shimmer and swirl of a rotary speaker |
| Flint | flint | hard stone that makes sparks when struck | sharp strike with a fast attack, suits a drum synth |
| Kobber | copper | conductive metal, wiring and circuits | warm analog synth with three filter circuits |

*Rav* means amber, not ember (ember is *glo*).

## Status of existing products

- **Mono will be renamed Kobber** (decided by Thomas, 3 October 2026; not yet scheduled). Mono is a polyphonic synth
  with three filters, so the old name is also misleading. Rename it only when asked.
- Rav, Glimmer and Mono still carry the Vekt prefix in `PRODUCT_NAME` ("Vekt Rav"), folders (`plugins/vekt_rav`),
  targets (`VektRav`) and some docs. Remove the prefix only when asked.
- New products start without the prefix; Flint is planned as `plugins/flint`.

Products are pre-release, so a rename does not yet break saved projects. It does change plugin and bundle identifiers,
preset folders, test labels and scripts that name the product, so plan it as its own change.
