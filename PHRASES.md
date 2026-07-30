# HOLYMESH broadcast phrase library

Everything the badge can broadcast over LoRa via the HOLYMESH scene, dumped
here for review.

- **COMPOSE mode** (default screen) — auto-generates a random 4-8 word GodWord
  sequence prefixed with `GOD SAYS:`. A rerolls, DOWN clears, B broadcasts,
  UP switches to BROWSE. Wire format `T1|C|<text>`.
- **BROWSE mode** — cycles the Terry aphorisms below. A broadcasts the
  currently-shown quote. Wire format `T1|Q|<text>`.
- **INBOX mode** — received messages log.

To trim: delete the lines you don't want, or mark them for removal, and I'll
apply the edits back to the source. To add new Terry aphorisms in the same
vein: append to the "Requests" section at the bottom.

---

## Terry aphorisms (19, verbatim only)

Source: `src/terry_quotes.h`. Rebuilt clean — every entry has a
verifiable source (Terry's own TempleOS docs, Wikiquote, or user-
supplied vlog notes). No composed / ~Terry-style entries anymore.

### TempleOS design constants (Terry's own docs)

- [ ] "An official God temple." — *TempleOS motto*
- [ ] "The Third Temple." — *TempleOS naming*
- [ ] "640x480 is God's chosen resolution." — *TempleOS spec*
- [ ] "16 colors is God's palette." — *TempleOS spec*
- [ ] "8x8 fonts. God is a monospace typeface." — *TempleOS design*
- [ ] "One user. One task. One machine." — *TempleOS design*
- [ ] "Ring 0 forever. Everyone is root." — *TempleOS design*

### Vlog / interview lines (Wikiquote)

- [ ] "An idiot admires complexity, a genius admires simplicity." — *Terry, vlog*
- [ ] "You banned me from Twitter, God bans you from Heaven." — *Terry, vlog*
- [ ] "God likes music that makes you feel." — *Terry, vlog*
- [ ] "I use Ubuntu to download VMware to run TempleOS." — *Terry, forum*
- [ ] "It's about a pathetic schizophrenic who made a crappy operating system." — *Terry, Motherboard*
- [ ] "What's reality? I don't know. When my bird was looking at my computer monitor I thought, 'That bird has no idea what he's looking at.'" — *Terry, vlog*

### User-supplied verbatim

- [ ] "Brontosaurs' feet hurt when stepped." — *Terry, vlog*
- [ ] "Thou shall not litter." — *Terry, vlog*
- [ ] "I like elephants and God likes elephants." — *Terry, vlog*
- [ ] "Is this too much voodoo?" — *Terry, vlog*
- [ ] "This is voodoo; the question is - is this too much." — *Terry, vlog*
- [ ] "The first time you meet an angel you get a horrible beating." — *Terry, vlog*

---

## GodWords (~300)

Source: `src/vocab.h`. These are single-word GOD SAYS broadcasts. Grouped
here by rough theme for readability — the file itself has no groupings.

**Divine names / roles**
LORD · GOD · JEHOVAH · JESUS · CHRIST · MESSIAH · SAVIOUR · ALMIGHTY · HOSTS ·
HOLY · SPIRIT · FATHER · SON · LAMB · KING · PRIEST · PROPHET · APOSTLE ·
DISCIPLE · SHEPHERD · ANGEL · SERAPH · CHERUB · ARCHANGEL

**Adversaries**
SATAN · DEVIL · DEMON · BEAST · DRAGON · SERPENT · VIPER

**Virtues / concepts**
LOVE · MERCY · GRACE · PEACE · FAITH · HOPE · TRUTH · WISDOM · KNOWLEDGE ·
UNDERSTANDING · RIGHTEOUSNESS · GLORY · POWER · MAJESTY · JUDGMENT · JUSTICE ·
MERCIFUL · GRACIOUS · BLESSED · CURSED · DIVINE · SACRED · ETERNAL ·
EVERLASTING · INFINITE · HALLOWED · ANOINTED

**Actions of worship**
PRAY · PRAISE · BLESS · WORSHIP · BEHOLD · VERILY · AMEN · HEARKEN · HEAR ·
SEEK · ASK · RECEIVE · REPENT · BELIEVE · FORGIVE · REJOICE · MOURN · LAMENT ·
TREMBLE · MARVEL

**People**
ADAM · EVE · CAIN · ABEL · NOAH · ENOCH · METHUSELAH · ABRAHAM · SARAH · ISAAC ·
REBEKAH · JACOB · ESAU · JOSEPH · MOSES · AARON · MIRIAM · JOSHUA · CALEB ·
GIDEON · SAMSON · SAMUEL · SAUL · DAVID · SOLOMON · ELIJAH · ELISHA · ISAIAH ·
JEREMIAH · EZEKIEL · DANIEL · JONAH · JOB · RUTH · ESTHER · MARY · MARTHA ·
JOHN · MATTHEW · MARK · LUKE · PETER · ANDREW · JAMES · PAUL · JUDAS · STEPHEN

**Places**
EDEN · BABEL · UR · EGYPT · GOSHEN · MIDIAN · SINAI · HOREB · CANAAN · ISRAEL ·
JUDAH · JERUSALEM · BETHLEHEM · NAZARETH · GALILEE · JORDAN · JERICHO ·
SAMARIA · BABYLON · NINEVEH · SODOM · GOMORRAH · GALGAL · ROME · GETHSEMANE ·
GOLGOTHA · EMMAUS

**Ritual objects**
TEMPLE · ALTAR · ARK · TABERNACLE · MERCY-SEAT · CROSS · CROWN · THRONE ·
SCEPTRE · SWORD · SHIELD · BOW · ARROW · STAFF · ROD · MANTLE · ROBE · SANDAL ·
VEIL · CANDLE · LAMP · OIL · INCENSE · MYRRH · FRANKINCENSE · BALM

**Nature**
HEAVEN · EARTH · SEA · SKY · SUN · MOON · STAR · CLOUD · WATER · FIRE · LIGHT ·
DARKNESS · WIND · STORM · RAIN · FLOOD · SNOW · MOUNTAIN · VALLEY · WILDERNESS ·
DESERT · GARDEN · FIELD · VINE · TREE · BRANCH · LEAF · FRUIT · ROOT · ROCK ·
STONE · DUST

**Creatures**
LION · EAGLE · DOVE · RAVEN · SPARROW · LAMB · SHEEP · GOAT · OX · ASS ·
HORSE · COLT · FISH · LOCUST · SCORPION · OWL

**Food**
BREAD · WINE · HONEY · MILK · MANNA · GRAIN · SALT · HERB · FIG · OLIVE ·
POMEGRANATE · VINEGAR · CURD

**Salvation / doctrine**
SIN · SALVATION · REDEMPTION · ATONEMENT · RESURRECTION · GLORY · COVENANT ·
COMMANDMENT · LAW · GOSPEL · PSALM · PROVERB · PROPHECY · SCRIPTURE ·
TESTAMENT · EPISTLE · REVELATION · VISION · DREAM

**Body / self**
SOUL · SPIRIT · HEART · FLESH · BLOOD · BONE · HAND · EYE · MOUTH · EAR ·
TONGUE · VOICE

**Time**
DAY · NIGHT · YEAR · AGE · MORNING · EVENING · SABBATH · JUBILEE · ETERNITY ·
FOREVER · BEGINNING · END · GENERATION · SEASON

**Emotions**
JOY · SORROW · WRATH · ANGER · FEAR · JEALOUS · MEEK · HUMBLE · PROUD · WICKED ·
HOLY · PURE · CLEAN · UNCLEAN · LEPER · BLIND

**Ritual / covenant**
SACRIFICE · OFFERING · BURNT · MEAL · PEACE · THANK · SIN · WORD · NAME ·
GRACE · GIFT · PROMISE · BLESSING · OATH · VOW

**Units / measures**
THIRD-DAY · MILE · STADIA · CUBIT · OMER · EPHAH · SHEKEL · TALENT

**Events**
FLOOD · EXODUS · PLAGUE · LOCUSTS · MANNA · QUAIL · STONE · MAGI · ROOSTER ·
FIG-TREE · MUSTARD · WHEAT · TARE · REAP · SOW · SHEAF

---

## Requests

Add new phrases you'd like broadcast — in the same terse programming-mystic
voice as Terry's. I'll fold them into `terry_quotes.h` next round.

- (add here)
- (add here)
- (add here)
