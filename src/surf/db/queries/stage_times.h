// =====[ STAGE TIMES ]=====
// Standalone stage records: a player's best segment time per stage of a course, from any attempt (a failed run still counts).
// Speed is the speed when the stage was left (touching the next stage's start, or the finish).

constexpr char sqlite_stagetimes_create[] = R"(
    CREATE TABLE IF NOT EXISTS StageTimes (
        SteamID64 INTEGER NOT NULL,
        MapCourseID INTEGER NOT NULL,
        ModeID INTEGER NOT NULL,
        StyleIDFlags INTEGER NOT NULL,
        Stage INTEGER NOT NULL,
        RunTime REAL NOT NULL,
        Speed REAL NOT NULL DEFAULT -1,
        Created INTEGER NOT NULL DEFAULT CURRENT_TIMESTAMP,
        CONSTRAINT PK_StageTimes PRIMARY KEY (SteamID64, MapCourseID, ModeID, StyleIDFlags, Stage),
        CONSTRAINT FK_StageTimes_SteamID64 FOREIGN KEY (SteamID64) REFERENCES Players(SteamID64)
        ON UPDATE CASCADE ON DELETE CASCADE,
        CONSTRAINT FK_StageTimes_MapCourseID FOREIGN KEY (MapCourseID) REFERENCES MapCourses(ID)
        ON UPDATE CASCADE ON DELETE CASCADE,
        CONSTRAINT FK_StageTimes_Mode FOREIGN KEY (ModeID) REFERENCES Modes(ID)
        ON UPDATE CASCADE ON DELETE CASCADE)
)";

constexpr char mysql_stagetimes_create[] = R"(
    CREATE TABLE IF NOT EXISTS StageTimes (
        SteamID64 BIGINT UNSIGNED NOT NULL,
        MapCourseID INTEGER UNSIGNED NOT NULL,
        ModeID INTEGER UNSIGNED NOT NULL,
        StyleIDFlags INTEGER UNSIGNED NOT NULL,
        Stage INTEGER UNSIGNED NOT NULL,
        RunTime DOUBLE UNSIGNED NOT NULL,
        Speed DOUBLE NOT NULL DEFAULT -1,
        Created TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
        CONSTRAINT PK_StageTimes PRIMARY KEY (SteamID64, MapCourseID, ModeID, StyleIDFlags, Stage),
        CONSTRAINT FK_StageTimes_SteamID64 FOREIGN KEY (SteamID64) REFERENCES Players(SteamID64)
        ON UPDATE CASCADE ON DELETE CASCADE,
        CONSTRAINT FK_StageTimes_MapCourseID FOREIGN KEY (MapCourseID) REFERENCES MapCourses(ID)
        ON UPDATE CASCADE ON DELETE CASCADE,
        CONSTRAINT FK_StageTimes_Mode FOREIGN KEY (ModeID) REFERENCES Modes(ID)
        ON UPDATE CASCADE ON DELETE CASCADE)
)";

// Portable upsert-if-faster in two statements: improve an existing row, or insert when there is none.
constexpr char sql_stagetimes_update[] = R"(
    UPDATE StageTimes SET RunTime=%.7f, Speed=%.1f, Created=CURRENT_TIMESTAMP
        WHERE SteamID64=%llu AND MapCourseID=%d AND ModeID=%d AND StyleIDFlags=%llu AND Stage=%d AND RunTime > %.7f
)";

constexpr char sql_stagetimes_insert[] = R"(
    INSERT INTO StageTimes (SteamID64, MapCourseID, ModeID, StyleIDFlags, Stage, RunTime, Speed)
        SELECT %llu, %d, %d, %llu, %d, %.7f, %.1f FROM Players
        WHERE Players.SteamID64=%llu AND NOT EXISTS (SELECT 1 FROM StageTimes
            WHERE SteamID64=%llu AND MapCourseID=%d AND ModeID=%d AND StyleIDFlags=%llu AND Stage=%d)
)";

// Top times of one stage (no styles), with the player's alias.
constexpr char sql_stagetimes_top[] = R"(
    SELECT s.SteamID64, p.Alias, s.RunTime, s.Speed
        FROM StageTimes s
        INNER JOIN MapCourses mc ON mc.ID = s.MapCourseID
        INNER JOIN Maps ON Maps.ID = mc.MapID
        INNER JOIN Players p ON p.SteamID64 = s.SteamID64
        WHERE p.Cheater=0 AND Maps.Name='%s' AND mc.Name='%s' AND s.ModeID=%d AND s.StyleIDFlags=0 AND s.Stage=%d
        ORDER BY s.RunTime ASC
        LIMIT %d
)";

// A player's own stage best and its rank among non-cheaters.
constexpr char sql_stagetimes_own[] = R"(
    SELECT s.RunTime, s.Speed,
        (SELECT COUNT(*) + 1 FROM StageTimes s2
            INNER JOIN Players p2 ON p2.SteamID64 = s2.SteamID64
            WHERE p2.Cheater=0 AND s2.MapCourseID=s.MapCourseID AND s2.ModeID=s.ModeID AND s2.StyleIDFlags=0 AND s2.Stage=s.Stage
            AND s2.RunTime < s.RunTime) AS Rank
        FROM StageTimes s
        INNER JOIN MapCourses mc ON mc.ID = s.MapCourseID
        INNER JOIN Maps ON Maps.ID = mc.MapID
        WHERE s.SteamID64=%llu AND Maps.Name='%s' AND mc.Name='%s' AND s.ModeID=%d AND s.StyleIDFlags=0 AND s.Stage=%d
)";
