[简体中文](README.zh_CN.md)

# Skin artwork

The approved skins unlock independently: apprentice witch at 20 affection,
long-sleeved pajamas at 40, hooded loungewear at 60, and scholar dress (`scholar.png`) at 80.
Each 4×3 PNG sheet contains ten used poses followed by two spare poses. Their order matches
the pose table in main/whale_ui.c. Maid, witch, pajamas, and loungewear keep the blue hair bow;
the scholar dress uses a terracotta flower and black-ivory tassel clip while preserving the
original cobalt-blue hair, eyes, whale-fin ears, and tail.
maid-companion.png is the 3×2 source for the six default interaction poses.

These are completed image-generation results made with the project's existing
character artwork as reference, with clothing and proportions reviewed in this chat.
Character references and attribution remain in LICENSES/ATTRIBUTION.txt.
The web search informed costume exploration. Unadopted sailor and café drafts
are excluded from this asset directory.

Run tools/make_whale_outfits.py to rebuild native 196px LVGL `I8` (256-color BGRA palette +
1 byte/px index) sprites and their frame metadata. Only fully transparent borders are omitted;
stored offsets restore each pose on its original canvas without enlargement. Four skins × ten
poses occupy 1,277,752 bytes, rendered directly from DROM by a zero-heap LVGL draw unit without
extra runtime image buffers or cloud services. Tall witch poses are fitted directly from the
source with caption clearance.

Selected locked skins can be previewed. Confirm equips only owned skins, and
confirming the worn skin returns to the default maid. Selection never changes
the stored skin until confirmed. Session work states always use the default art.

The five wardrobe symbols depict the maid dress, witch uniform, button pajamas,
hooded onesie, and scholar capelet dress. Rebuild their 20px alpha masks with
tools/make_whale_skin_icons.py.

The 40-affection pajamas wear a plush whale-head sleeping hood.
The 60-affection loungewear uses a plain soft hood without eye-like dots or animal-face details.

Wardrobe display order is maid, long-sleeved pajamas, apprentice witch, loungewear, scholar dress.
Display positions map to stable saved skin IDs; affection thresholds remain attached to the skin.

The apprentice witch replaces the old hoodie while keeping its saved ID and 20-affection unlock.
Its costume is inspired by Little Witch Academia; character identity remains the project whale.
Reference: https://mikatan.goodsmile.info/en/2017/03/13/nendoroid-atsuko-kagari-little-witch-academia/
The taller witch poses include extra caption clearance.
