#include "QuestDataSnapshot.h"

#include "SKSE/Logger.h"

namespace {

    constexpr int         DEPTH_LIMIT = 16;
    constexpr std::size_t NODE_LIMIT = 20000;
    constexpr std::size_t MAX_MEMBERS_PER_OBJECT = 20000;

    using Node = QuestSnapshot::Node;

    Node CaptureValue(const RE::GFxValue& a_value, int a_depth, std::size_t& a_nodes);

    void CaptureMembers(const RE::GFxValue& a_value, int a_depth, std::size_t& a_nodes, std::vector<std::pair<std::string, Node>>& a_out) {
        a_value.VisitMembers([&](const char* a_name, const RE::GFxValue& a_member) {
            if (!a_name) {
                return;
            }
            if (a_out.size() >= MAX_MEMBERS_PER_OBJECT) {
                return;
            }
            a_out.emplace_back(a_name, CaptureValue(a_member, a_depth + 1, a_nodes));
        });
    }

    Node CaptureValue(const RE::GFxValue& a_value, int a_depth, std::size_t& a_nodes) {
        Node node;

        if (++a_nodes > NODE_LIMIT) {
            // Bail out with a harmless placeholder rather than building a runaway tree.
            node.kind = Node::Kind::kString;
            node.string = "...";
            return node;
        }

        if (a_value.IsUndefined()) {
            node.kind = Node::Kind::kUndefined;
            return node;
        }
        if (a_value.IsNull()) {
            node.kind = Node::Kind::kNull;
            return node;
        }
        if (a_value.IsBool()) {
            node.kind = Node::Kind::kBoolean;
            node.boolean = a_value.GetBool();
            return node;
        }
        if (a_value.IsNumber()) {
            node.kind = Node::Kind::kNumber;
            node.number = a_value.GetNumber();
            return node;
        }
        if (a_value.IsString()) {
            node.kind = Node::Kind::kString;
            if (const char* str = a_value.GetString(); str) {
                node.string = str;
            }
            return node;
        }
        if (a_value.IsStringW()) {
            node.kind = Node::Kind::kStringW;
            if (const wchar_t* str = a_value.GetStringW(); str) {
                node.stringW = str;
            }
            return node;
        }
        if (a_value.IsDisplayObject()) {
            // A live clip owned by the other movie. Never recurse into it - keep only the same
            // text form ActionScript's toString() would have produced.
            node.kind = Node::Kind::kString;
            node.string = a_value.ToString().c_str();
            return node;
        }
        if (a_value.IsArray()) {
            node.kind = Node::Kind::kArray;
            if (a_depth >= DEPTH_LIMIT) {
                return node;
            }
            const auto size = a_value.GetArraySize();
            node.elements.reserve(size);
            for (std::uint32_t i = 0; i < size; ++i) {
                RE::GFxValue element;
                if (a_value.GetElement(i, &element)) {
                    node.elements.push_back(CaptureValue(element, a_depth + 1, a_nodes));
                }
            }
            return node;
        }
        if (a_value.IsObject()) {
            node.kind = Node::Kind::kObject;
            if (a_depth >= DEPTH_LIMIT) {
                return node;
            }
            CaptureMembers(a_value, a_depth, a_nodes, node.members);
            return node;
        }

        // Anything else (shouldn't happen for the journal's data) degrades to its text form.
        node.kind = Node::Kind::kString;
        node.string = a_value.ToString().c_str();
        return node;
    }

    bool BuildValue(RE::GFxMovieView* a_movie, const Node& a_node, RE::GFxValue& a_out) {
        if (!a_movie) {
            return false;
        }

        switch (a_node.kind) {
            case Node::Kind::kUndefined:
                a_out.SetUndefined();
                return true;
            case Node::Kind::kNull:
                a_out.SetNull();
                return true;
            case Node::Kind::kBoolean:
                a_out.SetBoolean(a_node.boolean);
                return true;
            case Node::Kind::kNumber:
                a_out.SetNumber(a_node.number);
                return true;
            case Node::Kind::kString:
                a_out.SetString(a_node.string);
                return true;
            case Node::Kind::kStringW:
                a_out.SetStringW(a_node.stringW);
                return true;
            case Node::Kind::kArray: {
                a_movie->CreateArray(&a_out);
                if (!a_out.IsArray()) {
                    return false;
                }
                for (const auto& element : a_node.elements) {
                    RE::GFxValue child;
                    if (!BuildValue(a_movie, element, child) || !a_out.PushBack(child)) {
                        return false;
                    }
                }
                return true;
            }
            case Node::Kind::kObject: {
                a_movie->CreateObject(&a_out);
                if (!a_out.IsObject()) {
                    return false;
                }
                for (const auto& [name, value] : a_node.members) {
                    RE::GFxValue child;
                    if (!BuildValue(a_movie, value, child) || !a_out.SetMember(name.c_str(), child)) {
                        return false;
                    }
                }
                return true;
            }
            default:
                return false;
        }
    }

}

QuestSnapshot::Node QuestSnapshot::Capture(const RE::GFxValue& a_value) {
    std::size_t nodes = 0;
    auto        snapshot = CaptureValue(a_value, 0, nodes);

    if (snapshot.kind == Node::Kind::kArray) {
        SKSE::log::info("Captured quest entry list: {} entries, {} values.",
                        snapshot.elements.size(), nodes);
    } else {
        SKSE::log::warn("titleList.entryList was not an array (type {}), snapshot has {} values.",
                        static_cast<int>(snapshot.kind), nodes);
    }

    return snapshot;
}

bool QuestSnapshot::Build(RE::GFxMovieView* a_movie, const Node& a_node, RE::GFxValue& a_out) {
    return BuildValue(a_movie, a_node, a_out);
}
