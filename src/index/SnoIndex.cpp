#include "index/SnoIndex.h"
#include "casc/CascReader.h"
#include "app/AppPaths.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cctype>

namespace {

// ── Group id → name, from the game's own group table, VALIDATED before adoption ─────────────
// REPLACED 2026-10-08, and the entry it replaces is worth reading first. The old table was built
// the only way then available: match each json/base/meta/<Folder> stem against CoreTOC and accept
// the owning group only where every unambiguous stem agrees. That method is sound, and every one
// of the 24 ids it could reach was right. Its LIMIT was never stated loudly enough — the sparse
// checkout ships 24 folders, so 140 ids had no folder to score against and silently kept whatever
// the Python fork had guessed. Scored against the table below: of 132 entries, 85 were WRONG and
// 12 more were "Unknown (N)" placeholders for ids whose real names were available all along.
//
// Where this table comes from: d4data ships json/snoGroups.json — the group id → name table, 167
// entries, ids -3..181. A shipped table is still a table (this file documents that trap below), so
// it was scored two independent ways BEFORE being adopted:
//
//   · Against the folder method above. 22 of the 24 folders agree exactly. The two that do not are
//     not contradictions: group 6's folder is "Anim" while the table says "Animation", and
//     PlayerClass (74) has no unambiguous stem for the folder method to score at all.
//   · Against the records themselves. Every sno reference in d4data carries its own __group__,
//     groupName and __targetFileName__ — the record's OWN answer about what it is, which no name
//     test can contradict. 1,770 records across seven folders yielded 64 distinct (id, groupName)
//     pairs and ZERO disagreements with snoGroups.json.
//
// No existing caller moves. The eight names anything actually asks for — Material 57, Texture 44,
// StringList 42, Appearance 9, MarkingShape 115, MarkingColor 133, StoreProduct 110, Item 73 —
// were all among the measured ones and all keep today's values.
//
// The expensive correction: 36 is ShaderMap, 37 is Shader, NOT the other way round. Groups 36 and
// 37 carry nearly the same names at adjacent SNO ids (hero_hair is 290837 in 36 and 290899 in 37),
// so "every shader name is in 37 and absent from 57" is perfectly true and still proves nothing:
// it rules out 57, not 36. A material's own reference settles it —
// Material.tUberMaterial.snoShaderMap declares __group__ 36, groupName "ShaderMap", target
// base/meta/ShaderMap/<name>.shm, in 60 of 60 sampled materials. Nothing visibly broke from the
// swap because MainWindow's material map spans both groups and the names agree either way, which
// is exactly why it survived. A test that cannot separate the candidate from its neighbour is not
// a test.
//
// Why any of this matters: all 18 callers of groupIdByName pass a hard-coded fallback, and the map
// WINS over that fallback. A wrong label therefore does not degrade to the fallback, it overrides
// it — groupIdByName("MarkingShape", 115) once returned 123 and the wardrobe's marking scan walked
// 468 territory records instead of 374 markings. Every wrong entry above was that bug waiting for
// its first caller.
//
// Two house rules kept: names are unique by construction, so groupIdByName's QHash walk is
// deterministic (the table's two Unknown_<n> entries are spelled "Unknown (n)" to match style and
// stay distinct); and the negative ids are sentinels, not groups (-1 None, -2 Code, -3 Unknown),
// so they are left out rather than given a row that groupIdByName could match.
const QHash<int, QString>& groupNameMap()
{
    static const QHash<int, QString> kMap = {
        {1,"Actor"}, {2,"NpcComponentSet"}, {3,"AiBehavior"}, {4,"AiState"}, {5,"AmbientSound"},
        {6,"Animation"}, {7,"Animation2D"}, {8,"AnimSet"}, {9,"Appearance"}, {10,"Hero"},
        {11,"Cloth"}, {12,"Conversation"}, {13,"ConversationList"}, {14,"EffectGroup"},
        {15,"Encounter"}, {17,"Explosion"}, {18,"FlagSet"}, {19,"Font"}, {20,"GameBalance"},
        {21,"Global"}, {22,"LevelArea"}, {23,"Light"}, {24,"MarkerSet"}, {26,"Observer"},
        {27,"Particle"}, {28,"Physics"}, {29,"Power"}, {31,"Quest"}, {32,"Rope"}, {33,"Scene"},
        {35,"Script"}, {36,"ShaderMap"}, {37,"Shader"}, {38,"Shake"}, {39,"SkillKit"}, {40,"Sound"},
        {42,"StringList"}, {43,"Surface"}, {44,"Texture"}, {45,"Trail"}, {46,"UI"}, {47,"Weather"},
        {48,"World"}, {49,"Recipe"}, {51,"Condition"}, {52,"TreasureClass"}, {53,"Account"},
        {57,"Material"}, {59,"Lore"}, {60,"Reverb"}, {62,"Music"}, {63,"Tutorial"},
        {65,"ControlScheme"}, {67,"AnimTree"}, {68,"Vibration"}, {71,"wWiseSoundBank"},
        {72,"Speaker"}, {73,"Item"}, {74,"PlayerClass"}, {76,"FogVolume"}, {77,"Biome"}, {78,"Wall"},
        {79,"SoundTable"}, {80,"SubZone"}, {81,"MaterialValue"}, {82,"MonsterFamily"}, {83,"TileSet"},
        {84,"Population"}, {85,"MaterialValueSet"}, {86,"WorldState"}, {87,"Schedule"},
        {88,"VectorField"}, {89,"PvPMode"}, {90,"StoryBoard"}, {91,"POI"}, {92,"Territory"},
        {93,"AudioContext"}, {94,"VoProcess"}, {95,"DemonScroll"}, {96,"QuestChain"},
        {97,"LoudnessPreset"}, {98,"ItemType"}, {99,"Achievement"}, {100,"Crafter"},
        {101,"HoudiniParticlesSim"}, {102,"Movie"}, {103,"TiledStyle"}, {104,"Affix"},
        {105,"Reputation"}, {106,"ParagonNode"}, {107,"MonsterAffix"}, {108,"ParagonBoard"},
        {109,"SetItemBonus"}, {110,"StoreProduct"}, {111,"ParagonGlyph"}, {112,"ParagonGlyphAffix"},
        {114,"Challenge"}, {115,"MarkingShape"}, {116,"ItemRequirement"}, {117,"Boost"},
        {118,"Emote"}, {119,"Jewelry"}, {120,"PlayerTitle"}, {121,"Emblem"}, {122,"Dye"},
        {123,"FogofWar"}, {124,"ParagonThreshold"}, {125,"AiAwareness"}, {126,"TrackedReward"},
        {127,"CollisionSettings"}, {128,"Aspect"}, {129,"AbTest"}, {130,"Stagger"}, {131,"EyeColor"},
        {132,"Makeup"}, {133,"MarkingColor"}, {134,"HairColor"}, {135,"DungeonAffix"},
        {136,"Activity"}, {137,"Season"}, {138,"HairStyle"}, {139,"FacialHair"}, {140,"Face"},
        {141,"MercenaryClass"}, {142,"PassivePowerContainer"}, {143,"MountProfile"},
        {144,"AICoordinator"}, {145,"CrafterTab"}, {146,"TownPortalCosmetic"}, {147,"AxeTest"},
        {148,"Wizard"}, {149,"FootstepTable"}, {150,"Modal"}, {151,"CollectiblePower"},
        {152,"AppearanceSet"}, {153,"Preset"}, {154,"PreviewComposition"}, {155,"SpawnPool"},
        {156,"Raid"}, {157,"BattlePassTier"}, {158,"Zone"}, {159,"Unknown (159)"}, {160,"DeathKit"},
        {161,"Snippet"}, {162,"CommunityModifier"}, {163,"GenericNodeGraph"}, {164,"UserDefinedData"},
        {165,"DataStore"}, {166,"BehaviorContainer"}, {167,"ActorService"}, {168,"DamageRemap"},
        {169,"Vendor"}, {170,"GenericSkillTree"}, {171,"Unknown (171)"}, {172,"Crowd"},
        {173,"CrowdTemplates"}, {174,"CrowdPlacements"}, {175,"VisualRemap"}, {176,"PowerModifier"},
        {177,"UIDesignerNotification"}, {178,"HoudiniDigitalAsset"},
        {179,"HoudiniDigitalAssetPreset"}, {180,"Indicator"}, {181,"Pip"},
    };
    return kMap;
}

// Features intentionally kept out of the browser (data-group level), inherited verbatim from the
// Python fork's EXCLUDED_GROUPS, which described them as Powers (29/104), Quests (31/166) and
// Adventure (2).
//
// THREE OF THOSE FIVE LABELS WERE WRONG, and the ids are left unchanged on purpose. Against the
// validated table above they are: 29 Power (10,293 records) and 31 Quest (3,732) — both exactly
// what the fork meant — but 104 is Affix (6,329 records), 2 is NpcComponentSet (255) and 166 is
// BehaviorContainer (3). There is no "Adventure" group at all, the real second power group is 176
// PowerModifier (44 records), and the real QuestChain is 96 (94 records) — none of which is
// excluded today.
//
// So this set no longer means what its comment used to claim: it hides 6,329 Affix records the
// fork never intended to hide, and does not hide the quest chains it thought it was hiding.
// Correcting the IDS changes what the browser shows, which is a product decision and not a label
// fix, so it is flagged here rather than made silently. Decide it deliberately; do not "tidy" it.
//
// 110 WAS in this list, on the fork's belief that it is a third Power group. It is
// StoreProductDefinition — the entire Cosmetics Shop, 9,308 records including 1,628 bundles — so
// excluding it hid every store product from the index and made a Catalogue tab impossible. If 29
// or 104 ever need to be trusted, verify them against definitions.json the same way first.
const QSet<int>& excludedGroups()
{
    static const QSet<int> kExcluded = {2, 29, 31, 104, 166};
    return kExcluded;
}

}  // namespace

QString SnoIndex::groupName(int group)
{
    const auto& m = groupNameMap();
    auto it = m.constFind(group);
    return it != m.constEnd() ? it.value() : QStringLiteral("Group %1").arg(group);
}

// Reverse lookup so tabs can pin themselves to a group NAME instead of a magic number —
// if a game update ever renumbers the SNO groups, only the name map needs correcting and
// every caller follows. `fallback` keeps today's behavior when the name isn't found.
int SnoIndex::groupIdByName(const QString& name, int fallback)
{
    const auto& m = groupNameMap();
    for (auto it = m.constBegin(); it != m.constEnd(); ++it)
        if (it.value().compare(name, Qt::CaseInsensitive) == 0) return it.key();
    return fallback;
}

bool SnoIndex::isExcluded(int group) { return excludedGroups().contains(group); }

QString SnoIndex::nameForSno(int group, int sno) const
{
    if (sno <= 0) return {};
    auto cached = m_snoNameCache.constFind(group);
    if (cached == m_snoNameCache.constEnd()) {
        const auto git = m_byGroup.constFind(group);
        if (git == m_byGroup.constEnd()) return {};
        QHash<int, QString> map;
        map.reserve(git.value().size());
        for (const SnoEntry& e : git.value()) {
            // Placeholders are not names. Storing them would make the caller's "did the index know
            // this?" test succeed and hand back the very "~unnamed_<sno>" it was trying to replace.
            if (e.name.startsWith(QLatin1String("~unnamed_"))) continue;
            map.insert(e.snoId, e.name);
        }
        cached = m_snoNameCache.insert(group, std::move(map));
    }
    return cached.value().value(sno);
}

int SnoIndex::snoForName(int group, const QString& name) const
{
    if (name.isEmpty()) return 0;
    auto cached = m_nameSnoCache.constFind(group);
    if (cached == m_nameSnoCache.constEnd()) {
        const auto git = m_byGroup.constFind(group);
        if (git == m_byGroup.constEnd()) return 0;
        QHash<QString, int> map;
        map.reserve(git.value().size());
        for (const SnoEntry& e : git.value()) {
            if (e.name.startsWith(QLatin1String("~unnamed_"))) continue;   // not a real name
            map.insert(e.name.toLower(), e.snoId);
        }
        cached = m_nameSnoCache.insert(group, std::move(map));
    }
    return cached.value().value(name.toLower(), 0);
}

void SnoIndex::clear()
{
    m_byGroup.clear();
    m_snoNameCache.clear();
    m_nameSnoCache.clear();
    m_loaded = false;
    m_total  = 0;
}

bool SnoIndex::ingest(QHash<int, QVector<SnoEntry>>& parsed)
{
    m_byGroup.clear();
    m_snoNameCache.clear();
    m_nameSnoCache.clear();   // reverse map is derived from m_byGroup — never outlive it
    m_total = 0;
    m_loaded = false;
    if (parsed.isEmpty())
        return false;

    for (auto it = parsed.begin(); it != parsed.end(); ++it) {
        if (isExcluded(it.key()))
            continue;
        QVector<SnoEntry> entries = it.value();
        std::sort(entries.begin(), entries.end(),
                  [](const SnoEntry& a, const SnoEntry& b) {
                      return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
                  });
        m_total += entries.size();
        m_byGroup.insert(it.key(), std::move(entries));
    }
    m_loaded = !m_byGroup.isEmpty();
    return m_loaded;
}

bool SnoIndex::loadFromCasc(CascReader& casc)
{
    clear();
    const QByteArray data = casc.readFile(QStringLiteral("base/CoreTOC.dat"));
    qInfo().noquote() << "loadFromCasc: base/CoreTOC.dat ->" << data.size() << "bytes";
    if (data.isEmpty())
        return false;
    QHash<int, QVector<SnoEntry>> parsed = parseCoreToc(data);
    return ingest(parsed);
}

// ── Post-ingest disk cache — same compact-binary recipe as the CASC caches ───────────────────
// magic · sig · groupCount · per group [i32 id, u32 n, per entry i32 sno + u16 utf8-name].
// Stored AFTER exclude+sort, so a warm launch is a bulk read straight into m_byGroup.
static constexpr quint32 kTocCacheMagic = 0x544F4331;   // 'TOC1'

// See util/CacheVersioning.h for the rule this follows.
// v2: entry NAMES changed meaning — encrypted appearances now carry the name recovered from their
// cloth data instead of "~unnamed_<sno>". A v1 cache is still perfectly valid data, which is exactly
// the problem: it loads clean, the recovery pass is skipped because it only runs on a cache MISS,
// and the recovered pieces silently never appear. Bump the filename so one rebuild re-runs it.
// v3: names again — applyEncryptedNameDicts now resolves encrypted assets from the game's own
// EncryptedNameDict files, so a v2 cache is stale for the same reason v1 was: it loads clean, the
// naming pass only runs on a cache MISS, and thousands of assets stay ~unnamed with no symptom
// except being absent from every name-shaped roster.
static QString tocCachePath() { return AppPaths::dataDir() + QStringLiteral("/coretoc_v3.bin"); }

bool SnoIndex::loadFromCache(const QString& sig)
{
    clear();
    if (sig.isEmpty()) return false;
    // Superseded cache versions are dead weight, not history — v1 alone is 33 MB. Removed on the
    // first load after a bump so the data folder doesn't accumulate one full index per version.
    for (const char* old : {"/coretoc_v1.bin", "/coretoc_v2.bin"}) {
        const QString stale = AppPaths::dataDir() + QLatin1String(old);
        if (QFile::exists(stale) && QFile::remove(stale))
            qInfo().noquote() << "SnoIndex: removed superseded index cache" << stale;
    }
    QFile f(tocCachePath());
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray all = f.readAll();
    const char* p = all.constData();
    const char* end = p + all.size();
    auto need = [&](qint64 n) { return end - p >= n; };
    if (!need(8)) return false;
    if (qFromLittleEndian<quint32>(p) != kTocCacheMagic) return false;
    const quint32 sigLen = qFromLittleEndian<quint32>(p + 4);
    p += 8;
    if (!need(sigLen)) return false;
    if (QString::fromUtf8(p, int(sigLen)) != sig) return false;   // different game build
    p += sigLen;
    if (!need(4)) return false;
    const quint32 groups = qFromLittleEndian<quint32>(p);
    p += 4;
    for (quint32 g = 0; g < groups; ++g) {
        if (!need(8)) { clear(); return false; }
        const qint32 id = qint32(qFromLittleEndian<quint32>(p)); p += 4;
        const quint32 n = qFromLittleEndian<quint32>(p); p += 4;
        QVector<SnoEntry> entries;
        entries.reserve(int(n));
        for (quint32 i = 0; i < n; ++i) {
            if (!need(6)) { clear(); return false; }
            SnoEntry e;
            e.snoId = qint32(qFromLittleEndian<quint32>(p)); p += 4;
            const quint16 nl = qFromLittleEndian<quint16>(p); p += 2;
            if (!need(nl)) { clear(); return false; }
            e.name = QString::fromUtf8(p, nl); p += nl;
            entries.append(std::move(e));
        }
        m_total += entries.size();
        m_byGroup.insert(id, std::move(entries));
    }
    m_loaded = !m_byGroup.isEmpty();
    return m_loaded;
}

void SnoIndex::saveToCache(const QString& sig) const
{
    if (!m_loaded || sig.isEmpty()) return;
    const QString path = tocCachePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString part = path + QStringLiteral(".part");
    QFile f(part);
    if (!f.open(QIODevice::WriteOnly)) return;
    QByteArray out;
    out.reserve(32 * 1024 * 1024);
    auto put32 = [&](quint32 v) { quint32 le = qToLittleEndian(v); out.append(reinterpret_cast<const char*>(&le), 4); };
    auto put16 = [&](quint16 v) { quint16 le = qToLittleEndian(v); out.append(reinterpret_cast<const char*>(&le), 2); };
    put32(kTocCacheMagic);
    const QByteArray sigU = sig.toUtf8();
    put32(quint32(sigU.size()));
    out.append(sigU);
    put32(quint32(m_byGroup.size()));
    for (auto it = m_byGroup.constBegin(); it != m_byGroup.constEnd(); ++it) {
        put32(quint32(it.key()));
        put32(quint32(it.value().size()));
        for (const SnoEntry& e : it.value()) {
            const QByteArray nu = e.name.toUtf8();
            put32(quint32(e.snoId));
            put16(quint16(qMin(nu.size(), qsizetype(0xFFFF))));
            out.append(nu.left(0xFFFF));
        }
    }
    f.write(out);
    f.close();
    QFile::remove(path);
    QFile::rename(part, path);
    qInfo("SnoIndex: CoreTOC cached (%lld MB) — next launch skips the parse",
          qint64(out.size()) / (1024 * 1024));
}

// ── "Latest" snapshot/diff ───────────────────────────────────────────────────────────────────
// File: magic · snapSig · snapCount·[i32 sno] · newSig · newCount·[i32 sno].
// snap* = the full SNO set at snapSig (the baseline to diff against); new* = the additions computed
// at the last build transition (kept so re-opening at the same build shows the same "Latest" set).
static constexpr quint32 kLatestMagic   = 0x4C415431;   // 'LAT1' — v1, single slot
// Distinct magic as well as a distinct filename. The filename alone is the project's versioning
// rule and is sufficient in normal use, but the two layouts start with the same four bytes
// otherwise, so a v1 file that ever reached the v2 path would read its snapSig length as a slot
// COUNT and allocate from garbage rather than failing.
static constexpr quint32 kLatestMagicV2 = 0x4C415432;   // 'LAT2' — v2, one slot per product
// v2 holds one slot PER LINEAGE (product code). v1 held exactly one slot for the whole tool,
// which meant retail and PTR overwrote each other's baseline — see updateLatest. v1 is migrated
// into the current lineage on first use rather than discarded, so an existing "Latest" survives.
static QString latestPath()   { return AppPaths::dataDir() + QStringLiteral("/latest_v2.bin"); }
static QString latestPathV1() { return AppPaths::dataDir() + QStringLiteral("/latest_v1.bin"); }

// One product's baseline: the snapshot taken at `snapSig`, and the additions found when the
// build last changed (kept until the NEXT change so "Latest" is stable between patches).
struct LatestSlot {
    QString snapSig;
    QSet<int> snap;
    QString newSig;
    QSet<int> newSet;
};

// At most this many lineages are kept. Realistically there are two (retail + PTR); the cap only
// stops an unbounded file if product codes ever churn. Each slot is the full SNO id set, ~3 MB.
static constexpr int kMaxLatestLineages = 4;

// ── Build ledger ────────────────────────────────────────────────────────────────────────────────
// Append-only, deltas only. Kept in its own file rather than folded into latest_v2.bin because the
// two have different lifetimes: the snapshot is replaced every build, the ledger must never be.
static constexpr quint32 kHistMagic = 0x42484931;   // 'BHI1'
static QString histPath() { return AppPaths::dataDir() + QStringLiteral("/build_history_v1.bin"); }
// Per product. 32 patches is years of history at a few thousand SNOs each; the cap exists so the
// file cannot grow without limit, not because the data is expensive.
static constexpr int kMaxBuildRecords = 32;

static QVector<SnoIndex::BuildRecord> readHistory()
{
    QVector<SnoIndex::BuildRecord> out;
    QFile f(histPath());
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QByteArray all = f.readAll();
    const char* p = all.constData();
    const char* end = p + all.size();
    auto need = [&](qint64 n) { return end - p >= n; };
    auto rdStr = [&](QString& s) -> bool {
        if (!need(4)) return false;
        const quint32 n = qFromLittleEndian<quint32>(p); p += 4;
        if (!need(n)) return false;
        s = QString::fromUtf8(p, int(n)); p += n; return true;
    };
    if (!need(8) || qFromLittleEndian<quint32>(p) != kHistMagic) return out;
    p += 4;
    const quint32 count = qFromLittleEndian<quint32>(p); p += 4;
    for (quint32 i = 0; i < count; ++i) {
        SnoIndex::BuildRecord r;
        if (!rdStr(r.product) || !rdStr(r.buildId) || !rdStr(r.gameVersion)
            || !rdStr(r.prevBuildId) || !rdStr(r.prevGameVersion)) return {};
        if (!need(8)) return {};
        r.firstSeen = qint64(qFromLittleEndian<quint64>(p)); p += 8;
        if (!need(4)) return {};
        const quint32 n = qFromLittleEndian<quint32>(p); p += 4;
        if (!need(qint64(n) * 4)) return {};
        r.added.reserve(int(n));
        for (quint32 k = 0; k < n; ++k) { r.added.insert(qint32(qFromLittleEndian<quint32>(p))); p += 4; }
        out.append(std::move(r));
    }
    return out;
}

static void writeHistory(const QVector<SnoIndex::BuildRecord>& recs)
{
    const QString path = histPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString part = path + QStringLiteral(".part");
    QFile f(part);
    if (!f.open(QIODevice::WriteOnly)) return;
    QByteArray out;
    auto put32 = [&](quint32 v) { quint32 le = qToLittleEndian(v); out.append(reinterpret_cast<const char*>(&le), 4); };
    auto put64 = [&](quint64 v) { quint64 le = qToLittleEndian(v); out.append(reinterpret_cast<const char*>(&le), 8); };
    auto putStr = [&](const QString& s) { const QByteArray u = s.toUtf8(); put32(quint32(u.size())); out.append(u); };
    put32(kHistMagic);
    put32(quint32(recs.size()));
    for (const SnoIndex::BuildRecord& r : recs) {
        putStr(r.product); putStr(r.buildId); putStr(r.gameVersion);
        putStr(r.prevBuildId); putStr(r.prevGameVersion);
        put64(quint64(r.firstSeen));
        put32(quint32(r.added.size()));
        for (int v : r.added) put32(quint32(v));
    }
    f.write(out);
    f.close();
    QFile::remove(path);
    QFile::rename(part, path);
}

static void readLatestV1(QString& snapSig, QSet<int>& snap, QString& newSig, QSet<int>& newSet)
{
    QFile f(latestPathV1());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QByteArray all = f.readAll();
    const char* p = all.constData();
    const char* end = p + all.size();
    auto need = [&](qint64 n) { return end - p >= n; };
    if (!need(4) || qFromLittleEndian<quint32>(p) != kLatestMagic) return;
    p += 4;
    auto rdStr = [&](QString& out) -> bool {
        if (!need(4)) return false;
        const quint32 n = qFromLittleEndian<quint32>(p); p += 4;
        if (!need(n)) return false;
        out = QString::fromUtf8(p, int(n)); p += n; return true;
    };
    auto rdSet = [&](QSet<int>& out) -> bool {
        if (!need(4)) return false;
        const quint32 n = qFromLittleEndian<quint32>(p); p += 4;
        if (!need(qint64(n) * 4)) return false;
        out.reserve(int(n));
        for (quint32 i = 0; i < n; ++i) { out.insert(qint32(qFromLittleEndian<quint32>(p))); p += 4; }
        return true;
    };
    if (!rdStr(snapSig) || !rdSet(snap) || !rdStr(newSig) || !rdSet(newSet)) {
        snapSig.clear(); snap.clear(); newSig.clear(); newSet.clear();   // corrupt → treat as absent
    }
}

// Read every lineage slot. Falls back to the v1 single-slot file, attributing it to `lineage`,
// so upgrading does not silently reset a baseline the user has been accumulating.
//
// NB: this map is NOT named `slots`. Qt defines `slots` as an empty macro in qobjectdefs.h (it is
// what makes "public slots:" work), so `QHash<...> slots;` expands to `QHash<...> ;` and every
// `slots.insert(...)` becomes `.insert(...)`. The compiler then reports "syntax error: '.'" on
// lines that are perfectly correct, which sends you looking anywhere but the variable name.
// `signals` and `emit` are the same trap.
static QHash<QString, LatestSlot> readLatestAll(const QString& lineage)
{
    QHash<QString, LatestSlot> lineages;
    QFile f(latestPath());
    if (f.open(QIODevice::ReadOnly)) {
        const QByteArray all = f.readAll();
        const char* p = all.constData();
        const char* end = p + all.size();
        auto need = [&](qint64 n) { return end - p >= n; };
        auto rdStr = [&](QString& out) -> bool {
            if (!need(4)) return false;
            const quint32 n = qFromLittleEndian<quint32>(p); p += 4;
            if (!need(n)) return false;
            out = QString::fromUtf8(p, int(n)); p += n; return true;
        };
        auto rdSet = [&](QSet<int>& out) -> bool {
            if (!need(4)) return false;
            const quint32 n = qFromLittleEndian<quint32>(p); p += 4;
            if (!need(qint64(n) * 4)) return false;
            out.reserve(int(n));
            for (quint32 i = 0; i < n; ++i) { out.insert(qint32(qFromLittleEndian<quint32>(p))); p += 4; }
            return true;
        };
        if (need(8) && qFromLittleEndian<quint32>(p) == kLatestMagicV2) {
            p += 4;
            const quint32 count = qFromLittleEndian<quint32>(p); p += 4;
            for (quint32 i = 0; i < count; ++i) {
                QString key; LatestSlot s;
                if (!rdStr(key) || !rdStr(s.snapSig) || !rdSet(s.snap)
                    || !rdStr(s.newSig) || !rdSet(s.newSet)) {
                    return {};   // corrupt → treat the whole file as absent, never half-trust it
                }
                lineages.insert(key, std::move(s));
            }
            return lineages;
        }
        return {};
    }
    // No v2 yet — migrate v1 under the CURRENT lineage. v1 was written by a build that could only
    // ever have had one product open, so attributing it to whatever is open now is correct.
    LatestSlot s;
    readLatestV1(s.snapSig, s.snap, s.newSig, s.newSet);
    if (!s.snapSig.isEmpty()) lineages.insert(lineage, std::move(s));
    return lineages;
}

static void writeLatestAll(const QHash<QString, LatestSlot>& lineages)
{
    const QString path = latestPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString part = path + QStringLiteral(".part");
    QFile f(part);
    if (!f.open(QIODevice::WriteOnly)) return;
    QByteArray out;
    auto put32 = [&](quint32 v) { quint32 le = qToLittleEndian(v); out.append(reinterpret_cast<const char*>(&le), 4); };
    auto putStr = [&](const QString& s) { const QByteArray u = s.toUtf8(); put32(quint32(u.size())); out.append(u); };
    auto putSet = [&](const QSet<int>& s) { put32(quint32(s.size())); for (int v : s) put32(quint32(v)); };
    put32(kLatestMagicV2);
    put32(quint32(lineages.size()));
    for (auto it = lineages.constBegin(); it != lineages.constEnd(); ++it) {
        putStr(it.key());
        putStr(it.value().snapSig); putSet(it.value().snap);
        putStr(it.value().newSig);  putSet(it.value().newSet);
    }
    f.write(out);
    f.close();
    QFile::remove(path);
    QFile::rename(part, path);
    // Only once v2 is safely on disk. Removing it earlier would lose the baseline if the write
    // failed, and the whole point of the migration is not to reset anyone's "Latest".
    QFile::remove(latestPathV1());
}

void SnoIndex::updateLatest(const QString& sig, const QString& lineage, const QString& gameVersion)
{
    m_newSnos.clear();
    if (!m_loaded || sig.isEmpty()) return;   // Latest needs a build id to detect updates

    // Empty lineage (Steam installs carry no product code) shares one slot, which is the old
    // behaviour and correct as long as only one product is ever opened.
    const QString key = lineage;

    // Append to the ledger unless this build is already recorded. Done here because this is the
    // one place that knows both the previous build and the current one — reconstructing that pair
    // later is impossible, which is the whole reason the ledger has to be written as it happens.
    auto recordBuild = [&](const QString& prevId, const QString& prevVer, const QSet<int>& added) {
        QVector<BuildRecord> hist = readHistory();
        for (const BuildRecord& r : hist)
            if (r.buildId == sig && r.product == key) return;   // already have it; never duplicate
        BuildRecord r;
        r.product = key;
        r.buildId = sig;
        r.gameVersion = gameVersion;
        r.prevBuildId = prevId;
        r.prevGameVersion = prevVer;
        r.firstSeen = QDateTime::currentSecsSinceEpoch();
        r.added = added;
        hist.append(std::move(r));
        // Trim oldest-first, per product, so one product's history cannot evict another's.
        int n = 0;
        for (const BuildRecord& x : hist) if (x.product == key) ++n;
        while (n > kMaxBuildRecords) {
            for (int i = 0; i < hist.size(); ++i)
                if (hist[i].product == key) { hist.remove(i); break; }
            --n;
        }
        writeHistory(hist);
    };

    QSet<int> cur;
    for (auto it = m_byGroup.constBegin(); it != m_byGroup.constEnd(); ++it)
        for (const SnoEntry& e : it.value()) cur.insert(e.snoId);

    QHash<QString, LatestSlot> lineages = readLatestAll(key);
    // A COPY, not a reference into the hash: the cap below erases entries, which would invalidate
    // any reference held across it. Qt containers are copy-on-write, so this costs nothing until
    // written, and it never is.
    const LatestSlot prev = lineages.value(key);

    if (prev.snapSig.isEmpty()) {    // first run for THIS product → baseline; nothing new yet
        // Never let unfamiliar product codes grow the file without bound. Capped BEFORE the insert,
        // and the loop breaks when nothing but our own key is left rather than spinning.
        while (lineages.size() >= kMaxLatestLineages) {
            bool erased = false;
            for (auto it = lineages.begin(); it != lineages.end(); ++it)
                if (it.key() != key) { lineages.erase(it); erased = true; break; }
            if (!erased) break;
        }
        lineages.insert(key, LatestSlot{sig, cur, sig, {}});
        writeLatestAll(lineages);
        // Baseline: recorded with no predecessor, so the UI can say "first build seen" rather
        // than implying nothing was added in it.
        recordBuild(QString(), QString(), {});
        qInfo().noquote() << QStringLiteral("SnoIndex: Latest — baseline established for product '%1'")
                                 .arg(key.isEmpty() ? QStringLiteral("(unknown)") : key);
        return;
    }
    if (prev.snapSig == sig) {       // same build as this product's snapshot → keep the last diff
        m_newSnos = prev.newSet;
        // Backfill the ledger for a build whose diff was computed BEFORE the ledger existed (or by
        // an older version of this tool). The additions are known — they are sitting in newSet —
        // only the predecessor's identity is not, because v1 never stored it. Recording it with an
        // empty prevBuildId is the honest form: the UI shows "previous build unknown" rather than
        // inventing one. Without this the ledger would be empty until the next game patch, and a
        // build selector with nothing in it looks broken rather than new.
        recordBuild(QString(), QString(), m_newSnos);
        return;
    }
    // Build changed WITHIN this product → additions. Cross-product diffs cannot happen: the
    // snapshot compared against is this product's own.
    for (int s : cur) if (!prev.snap.contains(s)) m_newSnos.insert(s);

    // The version of the build we are diffing AGAINST, so a non-consecutive pair can be labelled
    // honestly ("added between 2.3.0 and 2.3.2") instead of being passed off as a single patch.
    QString prevVer;
    for (const BuildRecord& r : readHistory())
        if (r.product == key && r.buildId == prev.snapSig) { prevVer = r.gameVersion; break; }
    recordBuild(prev.snapSig, prevVer, m_newSnos);

    lineages.insert(key, LatestSlot{sig, cur, sig, m_newSnos});
    writeLatestAll(lineages);
    qInfo().noquote() << QStringLiteral("SnoIndex: Latest — %1 new asset(s) since the previous %2 build")
                             .arg(m_newSnos.size())
                             .arg(key.isEmpty() ? QStringLiteral("game") : key);
}

QVector<SnoIndex::BuildRecord> SnoIndex::buildHistory(const QString& product) const
{
    QVector<BuildRecord> all = readHistory();
    QVector<BuildRecord> out;
    out.reserve(all.size());
    for (const BuildRecord& r : all)
        if (product.isEmpty() || r.product == product) out.append(r);
    // Newest first — the order a dropdown wants, and firstSeen is the only ordering that survives
    // a ledger written across many sessions.
    std::sort(out.begin(), out.end(),
              [](const BuildRecord& a, const BuildRecord& b) { return a.firstSeen > b.firstSeen; });
    return out;
}

bool SnoIndex::loadFromD4data(const QString& d4dataDir)
{
    clear();
    if (d4dataDir.isEmpty())
        return false;
    const QString path = QDir(d4dataDir).filePath(QStringLiteral("json/base/CoreTOC.dat.json"));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << "loadFromD4data: cannot open" << path;
        return false;
    }
    qInfo().noquote() << "loadFromD4data: parsing" << path;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning().noquote() << "loadFromD4data: JSON parse error:" << err.errorString();
        return false;
    }

    // { "<groupId>": { "<snoId>": "<name>", … }, … }
    QHash<int, QVector<SnoEntry>> parsed;
    const QJsonObject root = doc.object();
    for (auto git = root.constBegin(); git != root.constEnd(); ++git) {
        bool ok = false;
        const int gid = git.key().toInt(&ok);
        if (!ok)
            continue;
        const QJsonObject entries = git.value().toObject();
        QVector<SnoEntry>& vec = parsed[gid];
        vec.reserve(entries.size());
        for (auto eit = entries.constBegin(); eit != entries.constEnd(); ++eit) {
            bool ok2 = false;
            const int sno = eit.key().toInt(&ok2);
            if (!ok2) continue;
            // Same treatment as the binary CoreTOC path: an encrypted record arrives with no name,
            // and keeping it under an empty one is no better than dropping it — it sorts to the top
            // of every list and matches nothing. Synthesise the same "~unnamed_<sno>" so the two
            // load routes cannot disagree about what the index contains.
            QString nm = eit.value().toString().trimmed();
            if (nm.isEmpty()) nm = QStringLiteral("~unnamed_%1").arg(sno);
            vec.append(SnoEntry{sno, nm});
        }
    }
    return ingest(parsed);
}

QList<int> SnoIndex::groups() const
{
    QList<int> keys = m_byGroup.keys();
    std::sort(keys.begin(), keys.end());
    return keys;
}

const QVector<SnoEntry>& SnoIndex::entries(int group) const
{
    static const QVector<SnoEntry> kEmpty;
    auto it = m_byGroup.constFind(group);
    return it != m_byGroup.constEnd() ? it.value() : kEmpty;
}

// ── Encrypted-name recovery (see SnoIndex.h) ────────────────────────────────────────────────────
namespace {

// The shape the wardrobe roster requires: 3 class letters + gender, a style token ending in digits,
// and one of the five armour slot codes. Anchored and case-insensitive — "DruM_stor235_GLV" and
// "necF_stor245_TRS" both pass, "mnt_stor212_trophy" and "twoHandSorcStaff_stor063Shape" both fail.
// Anything that fails is left nameless: a wrong name is worse than none, because it would seat a
// piece on the wrong class.
const QRegularExpression& wardrobeShapeRe()
{
    static const QRegularExpression re(
        QStringLiteral("^[A-Za-z]{3}[FfMm]_[A-Za-z]+[0-9]+_(HLM|TRS|GLV|LEG|BTS)$"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Pull the authored name out of a decrypted Appearance payload. Scans for identifier-shaped ASCII
// runs (packed floats are incidentally printable — "B3?lu6?", "#<fff?" — hence the letter-run test),
// then trims trailing part tokens until what is left is a wardrobe-shaped name.
QString nameFromPayload(const QByteArray& blob)
{
    QByteArray cur;
    QString best;
    auto consider = [&best](const QByteArray& raw) {
        if (raw.size() < 6) return;
        if (!(std::isalpha(uchar(raw[0])) || raw[0] == '_')) return;
        int run = 0, longest = 0;
        for (char ch : raw) {
            const uchar u = uchar(ch);
            if (!(std::isalnum(u) || ch == '_')) return;
            if (std::isalpha(u)) longest = qMax(longest, ++run); else run = 0;
        }
        if (longest < 3) return;
        // "necM_stor245_LEG_chain" -> drop "chain" -> "necM_stor245_LEG". Trailing tokens vary
        // ("_cape", "_loin", "_fur_HQO"), so peel from the right until the shape matches.
        QStringList tok = QString::fromLatin1(raw).split(QLatin1Char('_'), Qt::SkipEmptyParts);
        while (tok.size() >= 3) {
            const QString cand = tok.join(QLatin1Char('_'));
            if (wardrobeShapeRe().match(cand).hasMatch()) {
                // Every cloth block in one appearance names the same piece, so a second opinion is
                // corroboration; a DISAGREEMENT means the scan caught something else and the whole
                // record is abandoned rather than resolved arbitrarily.
                if (best.isEmpty()) best = cand;
                else if (best.compare(cand, Qt::CaseInsensitive) != 0) best = QStringLiteral("!");
                return;
            }
            tok.removeLast();
        }
    };
    for (int i = 0; i <= blob.size(); ++i) {
        const uchar c = (i < blob.size()) ? uchar(blob[i]) : 0;
        if (c >= 0x20 && c < 0x7f) { cur.append(char(c)); continue; }
        consider(cur);
        cur.clear();
    }
    return best == QLatin1String("!") ? QString() : best;
}

}  // namespace

int SnoIndex::applyEncryptedNameDicts(CascReader& casc)
{
    if (!casc.isReady()) return 0;
    constexpr quint32 kMagic = 0xABCD4567u;

    // Which of the 189 dicts can we actually read? Only the ones whose key we hold. The file's hex
    // id is the key name BYTE-SWAPPED (0E5332FB2D834BBD -> …-0xbd4b832dfb32530e), but rather than
    // depend on that convention surviving a patch, both orders are accepted and matched against the
    // paths the TVFS actually lists. If nothing matches — a renamed scheme — fall back to trying
    // every dict and letting the magic check reject the ones we cannot decrypt.
    const QStringList all = casc.rootPathsWithPrefix(QStringLiteral("base/encryptednamedict-"));
    if (all.isEmpty()) return 0;
    QSet<QString> wanted;
    for (const QByteArray& kn : casc.tactKeyNames()) {
        QByteArray rev = kn;
        std::reverse(rev.begin(), rev.end());
        wanted.insert(QString::fromLatin1(kn.toHex()).toLower());
        wanted.insert(QString::fromLatin1(rev.toHex()).toLower());
    }
    QStringList todo;
    for (const QString& p : all) {
        const qsizetype at = p.lastIndexOf(QLatin1String("-0x"));
        const QString id = at >= 0 ? p.mid(at + 3, 16).toLower() : QString();
        if (wanted.contains(id)) todo << p;
    }
    if (todo.isEmpty()) todo = all;

    // sno -> authored name. SNO ids are unique across groups in D4, so the group field is only
    // needed as a cross-check; a dict entry whose group disagrees with the index is dropped rather
    // than trusted, because a name landing on the wrong asset is worse than no name.
    QHash<int, QPair<int, QString>> byId;   // sno -> (group, name)
    int filesRead = 0, filesRejected = 0;
    for (const QString& path : qAsConst(todo)) {
        const QByteArray blob = casc.readFile(path);
        if (blob.size() < 8) { ++filesRejected; continue; }
        const uchar* d = reinterpret_cast<const uchar*>(blob.constData());
        if (qFromLittleEndian<quint32>(d) != kMagic) { ++filesRejected; continue; }
        const int count = int(qFromLittleEndian<quint32>(d + 4));
        const qint64 tableEnd = 8 + qint64(count) * 8;
        if (count <= 0 || tableEnd > blob.size()) { ++filesRejected; continue; }
        ++filesRead;
        qint64 nameAt = tableEnd;
        for (int i = 0; i < count; ++i) {
            const int grp = int(qFromLittleEndian<qint32>(d + 8 + i * 8));
            const int sno = int(qFromLittleEndian<qint32>(d + 8 + i * 8 + 4));
            if (nameAt >= blob.size()) break;          // truncated blob: keep what we parsed
            const qsizetype nul = blob.indexOf('\0', qsizetype(nameAt));
            const qsizetype end = nul < 0 ? blob.size() : nul;
            const QString nm = QString::fromLatin1(blob.constData() + nameAt, int(end - nameAt)).trimmed();
            nameAt = end + 1;
            if (!nm.isEmpty()) byId.insert(sno, qMakePair(grp, nm));
        }
    }
    if (byId.isEmpty()) {
        qInfo("SnoIndex: encrypted name dicts — %d in build, %d readable, none yielded names",
              int(all.size()), filesRead);
        return 0;
    }

    int applied = 0, wrongGroup = 0, collided = 0;
    for (auto git = m_byGroup.begin(); git != m_byGroup.end(); ++git) {
        QSet<QString> taken;
        for (const SnoEntry& e : qAsConst(git.value()))
            if (!e.name.startsWith(QLatin1String("~unnamed_"))) taken.insert(e.name.toLower());
        bool touched = false;
        for (SnoEntry& e : git.value()) {
            if (!e.name.startsWith(QLatin1String("~unnamed_"))) continue;
            const auto hit = byId.constFind(e.snoId);
            if (hit == byId.constEnd()) continue;
            if (hit.value().first != git.key()) { ++wrongGroup; continue; }
            const QString nm = hit.value().second;
            if (taken.contains(nm.toLower())) { ++collided; continue; }
            taken.insert(nm.toLower());
            e.name = nm;
            ++applied;
            touched = true;
        }
        // ingest() sorts each group by name; renaming in place would otherwise leave the recovered
        // assets stranded at the '~' end of every list, present but impossible to find.
        if (touched)
            std::sort(git.value().begin(), git.value().end(),
                      [](const SnoEntry& a, const SnoEntry& b) {
                          return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
                      });
    }
    qInfo("SnoIndex: encrypted name dicts — %d in build, %d readable (%d rejected), %d names, "
          "%d applied, %d wrong group, %d duplicate",
          int(all.size()), filesRead, filesRejected, int(byId.size()), applied, wrongGroup, collided);
    // Names changed under it — the reverse map is now stale for every group this touched.
    m_snoNameCache.clear();
    m_nameSnoCache.clear();
    return applied;
}

int SnoIndex::recoverEncryptedNames(CascReader& casc)
{
    if (!casc.isReady()) return 0;
    constexpr int kGroupAppearance = 9;
    auto it = m_byGroup.find(kGroupAppearance);
    if (it == m_byGroup.end()) return 0;

    int nameless = 0, readable = 0, recovered = 0, collided = 0;
    // Names must stay unique: the roster resolves a piece by name, so two snos answering to
    // "necM_stor245_TRS" would make which one you get depend on iteration order.
    QSet<QString> taken;
    for (const SnoEntry& e : qAsConst(it.value()))
        if (!e.name.startsWith(QLatin1String("~unnamed_"))) taken.insert(e.name.toLower());

    for (SnoEntry& e : it.value()) {
        if (!e.name.startsWith(QLatin1String("~unnamed_"))) continue;
        ++nameless;
        const QByteArray blob = casc.readPayloadBySno(quint64(e.snoId));
        if (blob.isEmpty()) continue;   // no key held, or nothing stored
        ++readable;
        const QString nm = nameFromPayload(blob);
        if (nm.isEmpty()) continue;     // no cloth block, or nothing wardrobe-shaped in it
        if (taken.contains(nm.toLower())) { ++collided; continue; }
        taken.insert(nm.toLower());
        e.name = nm;
        ++recovered;
    }
    if (recovered > 0 || nameless > 0)
        qInfo("SnoIndex: encrypted-name recovery — %d nameless appearance(s), %d readable, "
              "%d name(s) recovered from cloth data, %d skipped as duplicates",
              nameless, readable, recovered, collided);
    // Names changed under it — the reverse map is now stale for every group this touched.
    m_snoNameCache.clear();
    m_nameSnoCache.clear();
    return recovered;
}
