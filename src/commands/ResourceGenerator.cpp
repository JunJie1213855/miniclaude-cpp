#include "commands/ResourceGenerator.h"
#include "llm/LlmClient.h"
#include "llm/DefaultLlmClient.h"
#include "llm/Provider.h"
#include "core/Message.h"
#include "core/Json.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace aicoder
{

  namespace
  {

    // Build a system prompt guiding the LLM based on resource kind.
    // The prompt instructs the model to emit a JSON object: {name, description, body}.
    std::string buildSystemPrompt(const std::string &kind, const std::string &desiredName)
    {
      std::string nameClause =
          "The 'name' field MUST be exactly \"" + desiredName + "\" (do not change it).";

      if (kind == "skill")
      {
        return std::string(
                   "You are a skill author for the aicoder CLI. Given a user hint and a desired name, "
                   "produce a single JSON object (and nothing else) with three fields: \n"
                   "  - name: string (the desired name, unchanged)\n"
                   "  - description: string (one concise sentence describing the skill)\n"
                   "  - body: string (the skill instruction text the LLM should follow when this skill is invoked; 50-300 words; "
                   "may include markdown). The skill will be stored at ~/.aicoder/skills/<name>/SKILL.md.\n") +
               nameClause + "\nReturn ONLY the JSON object, no prose, no code fences.";
      }
      if (kind == "command")
      {
        return std::string(
                   "You are a slash-command author for the aicoder CLI. Given a user hint and a desired name, "
                   "produce a single JSON object (and nothing else) with three fields: \n"
                   "  - name: string (the desired name, unchanged)\n"
                   "  - description: string (one concise sentence describing the command)\n"
                   "  - body: string (the command body / template text the LLM should expand when this command is invoked; "
                   "use the $ARGUMENTS placeholder where the user's arguments should be substituted; markdown allowed). "
                   "The command will be stored at ~/.aicoder/commands/<name>.md.\n") +
               nameClause + "\nReturn ONLY the JSON object, no prose, no code fences.";
      }
      if (kind == "agent")
      {
        return std::string(
                   "You are a sub-agent author for the aicoder CLI. Given a user hint and a desired name, "
                   "produce a single JSON object (and nothing else) with three fields: \n"
                   "  - name: string (the desired name, unchanged)\n"
                   "  - description: string (one concise sentence describing when this sub-agent should be invoked)\n"
                   "  - body: string (the system prompt that drives this sub-agent; describe role, goals, methodology, output format, "
                   "and explicit tool-selection guidance — which tools to prefer, when to escalate, when to stop). 100-400 words; markdown allowed.\n") +
               nameClause + "\nReturn ONLY the JSON object, no prose, no code fences.";
      }
      // "rule" (and fallback)
      return std::string(
                 "You are a rule author for the aicoder CLI. Given a user hint and a desired name, "
                 "produce a single JSON object (and nothing else) with three fields: \n"
                 "  - name: string (the desired name, unchanged)\n"
                 "  - description: string (one concise sentence describing the rule)\n"
                 "  - body: string (the rule text the LLM should always follow; concise, declarative, enforceable; markdown allowed).\n") +
             nameClause + "\nReturn ONLY the JSON object, no prose, no code fences.";
    }

    // Extract the first balanced top-level JSON object from a free-form text response.
    // Returns empty string if none found. Handles strings with escaped quotes so braces
    // inside string literals don't confuse the depth counter.
    std::string extractFirstJsonObject(const std::string &text)
    {
      const size_t n = text.size();
      size_t start = std::string::npos;
      for (size_t i = 0; i < n; ++i)
      {
        if (text[i] == '{')
        {
          start = i;
          break;
        }
      }
      if (start == std::string::npos)
        return "";

      int depth = 0;
      bool inString = false;
      bool escape = false;
      for (size_t i = start; i < n; ++i)
      {
        char c = text[i];
        if (inString)
        {
          if (escape)
          {
            escape = false;
          }
          else if (c == '\\')
          {
            escape = true;
          }
          else if (c == '"')
          {
            inString = false;
          }
          continue;
        }
        if (c == '"')
        {
          inString = true;
          continue;
        }
        if (c == '{')
        {
          ++depth;
        }
        else if (c == '}')
        {
          --depth;
          if (depth == 0)
          {
            return text.substr(start, i - start + 1);
          }
        }
      }
      return "";
    }

    std::string asString(const json &v)
    {
      if (v.is_string())
        return v.get<std::string>();
      if (v.is_null())
        return "";
      return v.dump();
    }

  } // namespace

  GeneratedResource generateResource(LlmClient &client,
                                     const std::string &kind,
                                     const std::string &userHint,
                                     const std::string &desiredName)
  {
    GeneratedResource out;
    out.name = desiredName;
    out.description = "User-created resource";
    out.body = "";

    std::vector<Message> msgs;
    msgs.push_back(systemText(buildSystemPrompt(kind, desiredName)));

    std::string userMsg =
        "Desired name: " + desiredName + "\n" +
        "User hint: " + (userHint.empty() ? "(none)" : userHint) + "\n" +
        "Produce the JSON object now.";
    msgs.push_back(userText(userMsg));

    Response resp;
    try
    {
      resp = client.send(msgs, {});
    }
    catch (const std::exception &)
    {
      // Bubble up empty body; caller can decide how to surface the failure.
      return out;
    }

    std::string text = assistantText(resp.assistant_message);
    if (text.empty())
      return out;

    std::string jsonText = extractFirstJsonObject(text);
    if (!jsonText.empty())
    {
      try
      {
        json parsed = json::parse(jsonText);
        // Force the requested name regardless of what the model echoed back.
        out.name = desiredName;
        if (parsed.contains("description"))
        {
          std::string d = asString(parsed["description"]);
          if (!d.empty())
            out.description = d;
        }
        if (parsed.contains("body"))
        {
          out.body = asString(parsed["body"]);
        }
        if (!out.body.empty())
          return out;
        // body missing/empty → fall through to heuristic fallback
      }
      catch (const std::exception &)
      {
        // parse failed → heuristic fallback
      }
    }

    // Heuristic fallback: treat the entire response as the body, keep defaults
    // for name/description.
    out.body = text;
    return out;
  }

} // namespace aicoder
