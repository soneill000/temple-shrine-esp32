# HOLYMESH broadcast phrase library

Everything the badge can broadcast over LoRa via the HOLYMESH scene, dumped
here for review. Two pools:

1. **Terry aphorisms** — from `src/terry_quotes.h`. Wire format `T1|Q|<text>`.
2. **GodWords** — from `src/vocab.h`. Wire format `T1|W|GOD SAYS: <word>`.

To trim: delete the lines you don't want, or mark them for removal, and I'll
apply the edits back to the source. To add new Terry aphorisms in the same
vein: append to the "Requests" section at the bottom.

---

## Terry aphorisms (34)

Source: `src/terry_quotes.h`. `~Terry-style` means composed to fit his
cadence — trim aggressively if any feel off-brand.

### TempleOS design principles

- [ ] "An official God temple." — *TempleOS motto*
- [ ] "The Third Temple." — *TempleOS naming*
- [ ] "640x480 is God's chosen resolution." — *TempleOS spec*
- [ ] "16 colors is God's palette." — *TempleOS spec*
- [ ] "8x8 fonts. God is a monospace typeface." — *TempleOS design*
- [ ] "One user. One task. One machine." — *TempleOS design*
- [ ] "No networking. No security. No excuses." — *TempleOS design*
- [ ] "Ring 0 forever. Everyone is root." — *TempleOS design*

### HolyC / programming

- [ ] "HolyC is like C plus assembly plus God." — *on HolyC*
- [ ] "Assembly language is a form of prayer." — *~Terry-style*
- [ ] "Compile in RAM. Divine speed." — *TempleOS build*
- [ ] "Fixed-point math. Floats are for cowards." — *~Terry-style*
- [ ] "Machine code is holy." — *on assembly*
- [ ] "Programming is a martial art." — *Terry, vlog*
- [ ] "A computer is a temple. Treat it well." — *~Terry-style*

### Divine intellect / God-computer

- [ ] "God is a random number generator." — *TempleOS RNG*
- [ ] "The RNG is how God speaks." — *TempleOS Oracle*
- [ ] "Ask God for a word." — *TempleOS GodWord*
- [ ] "God says whatever God wants." — *Terry, vlog*
- [ ] "The Bible is a book of stories, and stories are executable." — *~Terry-style*
- [ ] "Divine intellect requires divine hardware." — *~Terry-style*
- [ ] "Every commit is a prayer." — *~Terry-style*
- [ ] "God is not impressed by your framework." — *~Terry-style*

### On AI and modern computing

- [ ] "AI is stupid. Machines cannot reason." — *Terry, vlog*
- [ ] "There is no cloud. There is only the machine on your desk." — *~Terry-style*
- [ ] "Modern software is bloat on bloat." — *~Terry-style*
- [ ] "The best interface is a text editor." — *~Terry-style*

### Personal / philosophical

- [ ] "Talk in raw math." — *Terry, forum*
- [ ] "Every day, a new random word from God." — *TempleOS habit*
- [ ] "The mainframe of Heaven has a CLI." — *~Terry-style*
- [ ] "Write the operating system you would pray to." — *~Terry-style*
- [ ] "Rejoice. You have registers." — *~Terry-style*

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
