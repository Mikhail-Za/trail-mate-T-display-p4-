# Field Guide photo sourcing assignments

Shared rules for every group (read fully before starting):

- SOURCE: Wikimedia Commons ONLY. Search by the scientific name (Commons categories
  are curated per species, e.g. https://commons.wikimedia.org/wiki/Category:Typha_latifolia).
  Use the API with a User-Agent header, e.g.:
  `curl -s -A "TrailMateFieldGuide/1.0 (personal offline device)" "https://commons.wikimedia.org/w/api.php?action=query&format=json&generator=categorymembers&gcmtitle=Category:Typha_latifolia&gcmtype=file&gcmlimit=30&prop=imageinfo&iiprop=url|extmetadata&iiurlwidth=800"`
  Download the 800px thumbnail URLs (`thumburl`). If a category is sparse, use
  `generator=search&gsrsearch=<scientific name>&gsrnamespace=6` as fallback.
- LICENSE: accept CC0, public domain, CC BY, CC BY-SA (any version). Record what you
  find in extmetadata (Artist, LicenseShortName) per photo. Skip non-free.
- VISUAL VERIFICATION IS MANDATORY: after downloading each candidate, VIEW the image
  (Read tool). Keep it only if (a) it plausibly shows the assigned species, (b) the
  assigned feature is clearly visible, (c) it is sharp and well-lit, (d) it works at
  small size (500px wide on a 540px screen). Discard herbarium sheets, illustrations,
  maps, seed packets, and anything ambiguous. If unsure between candidates, view more
  and pick the clearest.
- OUTPUT per article: save the chosen photos as
  `C:\Users\zaidm\tdisplay-p4-dualmesh\trail-mate\tools\offline_content\photos_raw\<section>\<article-stem>\1.jpg` (then 2.jpg, 3.jpg, 4.jpg)
  numbered by the shot list order below. JPEG as downloaded (the pipeline re-sizes).
  If a Commons file is PNG, save it as .png with the same number; the pipeline converts.
- CREDITS: in each article folder also write `credits.tsv`, one row per kept photo:
  `<n>.jpg <TAB> <species shown> <TAB> <artist> <TAB> <license> <TAB> <commons file page URL>`
  Use the SOURCE photo number; PNG sources may use that same number with either
  `.png` or `.jpg` in credits. The builder renumbers photos and credits together.
  Every photo needs one complete credit row; duplicate numbers, extra/missing
  rows, empty fields, symlinks, and decks over six photos fail validation.
  Copy generated content to a card only after the builder reports STAGING_DONE.
- Shot lists say what each numbered photo should show. 2 photos minimum, 3-4 when the
  shot list has more entries and good candidates exist.
- Final message: "DONE <group> <total photos> photos across <n> articles" plus any
  article you could NOT satisfy and why.

## Group E1 (section plants_edible)
- `01-cattail` Typha latifolia: 1 whole stand with brown seed heads, 2 close-up of
  the cigar-shaped seed head, 3 base/shoots if available.
- `02-dandelion` Taraxacum officinale: 1 flowering rosette, 2 leaf close-up (toothed),
  3 seed head.
- `03-broadleaf-plantain` Plantago major: 1 whole rosette showing parallel veins,
  2 seed spike, 3 leaf close-up.
- `04-white-clover` Trifolium repens: 1 patch with flower heads, 2 leaf trifoliate
  close-up with pale chevrons.
- `05-chickweed` Stellaria media: 1 sprawling mat, 2 close-up of tiny star flowers
  (split petals), 3 stem showing the single hair line if findable.

## Group E2 (section plants_edible)
- `06-lambs-quarters` Chenopodium album: 1 whole plant, 2 leaf close-up showing white
  mealy coating (diamond/goosefoot shape).
- `07-wood-sorrel` Oxalis stricta: 1 patch, 2 close-up of heart-shaped trifoliate
  leaves + yellow flower.
- `08-common-blue-violet` Viola sororia: 1 flowering clump, 2 heart-shaped leaf close-up.
- `09-acorns-oaks` Quercus (any common NA species, e.g. Quercus alba/rubra): 1 acorns
  on tree or in hand, 2 oak leaf close-up.
- `10-pine` Pinus strobus: 1 needle bundles close-up (5-needle fascicles), 2 whole
  tree; PLUS 3 = Taxus (yew) branch close-up with red arils, labeled in credits as
  Taxus, because the article's deadly-lookalike warning needs the contrast.

## Group E3 (section plants_edible)
- `11-blackberry-raspberry` Rubus (fruticosus or allegheniensis for blackberry;
  idaeus for raspberry): 1 ripe aggregate berries on cane, 2 leaf + thorny cane.
- `12-blueberry-huckleberry` Vaccinium (corymbosum or angustifolium): 1 berry cluster
  showing the 5-pointed crown, 2 shrub with leaves.
- `13-rose-hips` Rosa (canina or rugosa): 1 red hips on bush, 2 hip close-up.
- `14-common-burdock` Arctium minus: 1 whole plant with huge leaves, 2 burrs/flower
  heads.
- `15-staghorn-sumac` Rhus typhina: 1 red upright berry cone, 2 velvety branch +
  compound leaves; PLUS 3 = Toxicodendron vernix (poison sumac) showing WHITE drooping
  berries, labeled in credits as Toxicodendron vernix, for the article's contrast
  warning.

## Group H1 (section plants_hazard)
- `01-contact-poison-ivy-oak-sumac`: 1 Toxicodendron radicans (poison ivy) leaves of
  three close-up, 2 Toxicodendron diversilobum (poison oak) foliage, 3 Toxicodendron
  vernix (poison sumac) foliage/white berries, 4 poison ivy in fall red color if a
  good one exists. Label each row in credits with its species.
- `02-contact-other`: 1 Urtica dioica (stinging nettle) close-up showing stinging
  hairs, 2 Heracleum mantegazzianum (giant hogweed) whole plant with person/scale if
  possible, 3 Pastinaca sativa (wild parsnip) yellow umbels.

## Group H2 (section plants_hazard)
- `03-deadly-water-hemlock` Cicuta maculata: 1 whole plant with white umbels,
  2 purple-streaked stem close-up.
- `04-deadly-poison-hemlock` Conium maculatum: 1 whole plant, 2 purple-blotched
  smooth stem close-up.
- `05-deadly-death-camas` Toxicoscordion venenosum (syn. Zigadenus venenosus):
  1 flowering plant, 2 cream/white flower raceme close-up.
- `08-mushroom-rule`: 1 Amanita bisporigera or Amanita phalloides (destroying angel /
  death cap) clean specimen photo, 2 Amanita phalloides showing the plain innocent
  look (cap top view). Label species in credits. These photos exist to make the
  no-mushroom rule visceral.

## Group H3 (section plants_hazard)
- `06-deadly-berries`: 1 Phytolacca americana (pokeweed) purple stem + berry raceme,
  2 Actaea pachypoda (white baneberry, doll's eyes) berry close-up, 3 Menispermum
  canadense (moonseed) berry cluster if a clear one exists. Label each row's species.
- `07-deadly-other`: 1 Datura stramonium (jimsonweed) trumpet flower, 2 Datura
  stramonium spiny seed pod, 3 Digitalis purpurea (foxglove) flower spike. Label
  each row's species.
