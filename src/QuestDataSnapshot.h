#ifndef QUEST_DATA_SNAPSHOT_H
#define QUEST_DATA_SNAPSHOT_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "RE/G/GFxMovieView.h"
#include "RE/G/GFxValue.h"

namespace QuestSnapshot {

    // A movie-independent copy of a GFxValue tree.
    //
    // Why this exists: QJO reads "entryList" out of the JournalMenu movie and hands it to the
    // QuestMenu movie through a file-level RE::GFxValue. That value is an AS object that belongs
    // to the JournalMenu's runtime, so once that movie is torn down (the journal hotkey path
    // closes it immediately) using the value from the QuestMenu movie walks a dead reference and
    // crashes in GASValue::DropRefs. Copying every scalar by value and rebuilding every
    // object/array here keeps nothing from the other movie alive.
    struct Node {
        enum class Kind {
            kUndefined,
            kNull,
            kBoolean,
            kNumber,
            kString,
            kStringW,
            kObject,
            kArray
        };

        Kind         kind{ Kind::kUndefined };
        bool         boolean{ false };
        double       number{ 0.0 };
        std::string  string;
        std::wstring stringW;
        std::vector<std::pair<std::string, Node>> members;   // kObject
        std::vector<Node>                         elements;  // kArray
    };

    // Deep-copies a_value into a Node tree. Display objects are never descended into (only their
    // text form is kept), and both nesting depth and node count are capped so a self-referencing
    // or unexpectedly huge AS object cannot spin or exhaust memory.
    Node Capture(const RE::GFxValue& a_value);

    // Rebuilds a_node inside a_movie's runtime. a_out is left undefined when this returns false.
    bool Build(RE::GFxMovieView* a_movie, const Node& a_node, RE::GFxValue& a_out);

}

#endif  // QUEST_DATA_SNAPSHOT_H
