#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Makes the wiki's "Game compatibility" page: one table of how games run in PC5, from two
places.

- A game that runs: a report in the "Game compatibility" discussions category, made with
  .github/DISCUSSION_TEMPLATE/game-compatibility.yml (GitHub writes its body as "### <label>"
  headings, each followed by its answer). It lists as posted: works perfectly, or playable with
  issues. Each one gets one comment once the page with it is published.
- A game that doesn't run: an issue in this repository, with the game's log. It lists by its
  state: being looked at while it is open; working perfectly once it is closed as completed
  (unless a player's report lists the game already); unresolved (it doesn't run) once it is
  closed as not planned. An issue closed as a duplicate
  isn't listed. The game is the issue's "Game" field (.github/ISSUE_TEMPLATE/1-game.yml, which
  PC5's Report on GitHub opens filled in), or the "**Game:**" line older PC5s wrote. For an
  issue written by hand, before the form, games-by-issue.txt beside this script names it, or
  a report names it.

A report that a game doesn't run, made before issues were the way (the form no longer offers
it), names its game's issue: without one it gets one comment saying so and is closed. The
category's "Game compatibility list" discussion (made the first time, for pinning) points to
the page and to the report form: an Action can make a discussion but not edit one afterwards.

Run by .github/workflows/compatibility.yml with GITHUB_TOKEN and GITHUB_REPOSITORY.
"""
import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

CATEGORY = "Game compatibility"
LIST_TITLE = "Game compatibility list"
RESULTS = {  # the form's answers, best first, with the mark the table shows
    "Works perfectly": "✅ Works perfectly",
    "Playable with issues": "🟡 Playable with issues",
    "Starts but not playable": "🟠 Starts but not playable",
    "Doesn't start": "❌ Doesn't start",
}
NOT_RUNNING = ("Starts but not playable", "Doesn't start")   # older reports: their issue lists the game
LOOKED_AT = "🔍 Being looked at"     # an issue still open
FIXED = "Works perfectly"           # an issue closed as completed
UNRESOLVED = "❌ Unresolved: doesn't run"   # an issue closed as not planned
SHOWN = ["Works perfectly", "Playable with issues", LOOKED_AT, UNRESOLVED]
MARK = "<!-- pc5-compatibility: needs an issue -->"   # the Action's comment, so it is made once
UNKNOWN = {}   # an issue that couldn't be looked up: the report waits for the next run
LISTED = "<!-- pc5-compatibility: listed -->"   # the Action's comment once a report is in the list


def graphql(query: str, fatal: bool = True, **variables):
    """The answer's data. An error stops the run, or, with fatal False, is a warning and None."""
    request = urllib.request.Request(
        "https://api.github.com/graphql",
        data=json.dumps({"query": query, "variables": variables}).encode(),
        headers={"Authorization": f"bearer {os.environ['GITHUB_TOKEN']}", "Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=60) as response:
        answer = json.load(response)
    if answer.get("errors") and not fatal:
        print(f"::warning::compatibility list: {json.dumps(answer['errors'])[:900]}")
        return None
    if answer.get("errors"):
        # As an annotation too, which the run's page shows without opening its log.
        print(f"::error::compatibility list: {json.dumps(answer['errors'])[:900]}")
        sys.exit(f"compatibility list: {answer['errors']}")
    return answer["data"]


LABELS = ("Game", "Store and version", "How it runs", "Issue", "PC5 version", "Controller mode",
          "Settings that helped", "Notes")


def answers(body: str) -> dict:
    """The form's answers by label; "" for one left empty. A heading with its answer on the same
    line, as a report edited by hand may have it ("### Issue #43"), counts too."""
    fields, label, lines = {}, None, []
    for line in body.replace("\r\n", "\n").split("\n"):
        heading = re.match(r"^ {0,3}###\s+(.+?)\s*$", line)   # Markdown allows 3 spaces before it
        if heading:
            if label is not None:
                fields[label] = "\n".join(lines).strip()
            label, lines = heading.group(1), []
            for known in LABELS:
                rest = label[len(known):]
                if label.casefold().startswith(known.casefold()) and rest[:1] in (" ", ":", "\t"):
                    label, lines = known, [rest.lstrip(" :\t")]
                    break
        elif label is not None:
            lines.append(line)
    if label is not None:
        fields[label] = "\n".join(lines).strip()
    return {key: "" if value == "_No response_" else value for key, value in fields.items()}


def cell(text: str, limit: int = 120) -> str:
    """One table cell: one line, no pipes, cut at limit characters."""
    text = " ".join(text.split()).replace("|", "/")
    return text if len(text) <= limit else text[:limit - 1].rstrip() + "…"


def main() -> int:
    owner, name = os.environ["GITHUB_REPOSITORY"].split("/")
    repo = graphql("""query($owner: String!, $name: String!) {
        repository(owner: $owner, name: $name) {
          id
          discussionCategories(first: 50) { nodes { id name } }
        } }""", owner=owner, name=name)["repository"]
    category = next((c for c in repo["discussionCategories"]["nodes"] if c["name"] == CATEGORY), None)
    if not category:
        print(f"compatibility list: no '{CATEGORY}' category yet: nothing to do")
        return 0

    reports, named, listing, after = [], {}, None, None
    while True:
        page = graphql("""query($owner: String!, $name: String!, $category: ID!, $after: String) {
            repository(owner: $owner, name: $name) {
              discussions(first: 100, after: $after, categoryId: $category, states: [OPEN, CLOSED],
                          orderBy: {field: CREATED_AT, direction: ASC}) {
                pageInfo { hasNextPage endCursor }
                nodes { id number title url body createdAt author { login } upvoteCount locked closed
                        comments(first: 50) { nodes { body author { login } } } }
              } } }""", owner=owner, name=name, category=category["id"], after=after)
        discussions = page["repository"]["discussions"]
        for discussion in discussions["nodes"]:
            if discussion["title"].strip() == LIST_TITLE:
                listing = listing or discussion
                continue
            fields = answers(discussion["body"] or "")
            game = fields.get("Game") or re.sub(r"^\[|\]$", "", discussion["title"]).strip()
            result = fields.get("How it runs", "")
            if not game or result not in RESULTS:
                continue   # not made with the form
            if result in NOT_RUNNING:
                # Listed by its issue, under this report's name for the game when the issue has none.
                issue = linked_issue(owner, name, fields.get("Issue", ""))
                asked = any(MARK in (c["body"] or "") for c in discussion["comments"]["nodes"])
                if issue is None and (discussion["author"] or {}).get("login", "").casefold() != owner.casefold():
                    needs_issue(discussion, owner, name, asked)
                elif issue and issue is not UNKNOWN:
                    named.setdefault(issue["number"], game)
                    if asked and discussion["closed"] and graphql(
                            """mutation($id: ID!) { reopenDiscussion(input: {discussionId: $id}) { discussion { id } } }""",
                            fatal=False, id=discussion["id"]) is not None:
                        print(f"compatibility list: #{discussion['number']} names issue #{issue['number']}: opened again")
                continue
            shown, issue = result, None
            told = any(LISTED in (c["body"] or "") for c in discussion["comments"]["nodes"])
            reports.append({
                "id": discussion["id"], "told": told, "locked": discussion["locked"],
                "game": game, "result": result, "shown": shown, "issue": issue, "pc5": fields.get("PC5 version", ""),
                "edition": fields.get("Store and version", ""), "controller": fields.get("Controller mode", ""),
                "settings": fields.get("Settings that helped", ""), "notes": fields.get("Notes", ""),
                "url": discussion["url"], "number": discussion["number"],
                "author": (discussion["author"] or {}).get("login", "someone"),
                "date": discussion["createdAt"][:10], "votes": discussion["upvoteCount"],
            })
        if not discussions["pageInfo"]["hasNextPage"]:
            break
        after = discussions["pageInfo"]["endCursor"]
    # A game with an issue, by how it stands; one fixed, only when no report lists it already.
    reported = {r["game"].casefold() for r in reports}
    reports += [r for r in issue_reports(owner, name, named) if r["shown"] != FIXED or r["game"].casefold() not in reported]

    reports.sort(key=lambda r: (r["game"].casefold(), SHOWN.index(r["shown"]), r["date"]))
    counts = {shown: sum(1 for r in reports if r["shown"] == shown) for shown in SHOWN}
    add = f"https://github.com/{owner}/{name}/discussions/new?category=game-compatibility"
    lines = [
        f"**[➕ Add a game]({add})**: the report form, which asks what this list needs.",
        "",
        ", ".join(f"{label(shown)} {counts[shown]}" for shown in SHOWN) + ".",
        "",
        "| Game | How it runs | PC5 | Store and version | Controller | Settings that helped | Notes | Report or issue |",
        "| --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for r in reports:
        how = label(r["shown"])
        lines.append(f"| {cell(r['game'], 60)} | {how} | {cell(r['pc5'], 12)} | {cell(r['edition'], 40)} | "
                     f"{cell(r['controller'], 20)} | {cell(r['settings'])} | {cell(r['notes'], 200)} | "
                     f"[#{r['number']}]({r['url']}) by "
                     f"{cell(r['author'], 40)}, {r['date']} |")
    if not reports:
        lines.append("| *No reports yet* | | | | | | | |")
    lines += ["", "A game that crashes, doesn't start or isn't playable: open an "
              f"[issue](https://github.com/{owner}/{name}/issues/new/choose) with its log (PC5's **Report on GitHub** "
              "writes one for you). The game shows as being looked at while the issue is open, then as working "
              "perfectly once the issue is closed as completed, or as unresolved (it doesn't run) once it is closed "
              "as not planned."]

    page = "\n".join([
        "Which games run in PC5: the games players report running, from the [Game compatibility]"
        f"(https://github.com/{owner}/{name}/discussions/categories/game-compatibility) discussions, and the games "
        f"with an [issue](https://github.com/{owner}/{name}/issues?q=is%3Aissue), by how it stands. A GitHub Action "
        "makes it again each time either changes.",
        "",
    ] + lines) + "\n"
    publish(owner, name, page)
    told = sum(tell_listed(r, owner, name) for r in reports
               if not r["told"] and not r["locked"] and r["author"].casefold() != owner.casefold())
    if told:
        print(f"::notice::compatibility list: told {told} report{'s' * (told != 1)} they are in the list")

    pointer = "\n".join([
        f"## ➕ [Add a game: tell how it runs]({add})",
        "",
        "That link opens the report form: the game's name, how it runs, your PC5 version and controller mode. "
        "Post it, and within a minute or two the game is in the list:",
        "",
        f"## 📋 [The game compatibility list](https://github.com/{owner}/{name}/wiki/Game-compatibility)",
        "",
        "Please don't reply here: each report is its own discussion.",
    ]) + "\n"
    if listing is None:
        made = graphql("""mutation($repo: ID!, $category: ID!, $title: String!, $body: String!) {
            createDiscussion(input: {repositoryId: $repo, categoryId: $category, title: $title, body: $body}) {
              discussion { url } } }""", repo=repo["id"], category=category["id"], title=LIST_TITLE, body=pointer)
        print(f"compatibility list: made {made['createDiscussion']['discussion']['url']} (pin it)")
    return 0


GAME_LINE = re.compile(r"^\s*(?:\*\*)?Game:(?:\*\*)?\s*(.+?)\s*$", re.I | re.M)
PC5_LINE = re.compile(r"^\s*(?:\*\*)?PC5:(?:\*\*)?\s*v?([0-9][0-9.]*)", re.I | re.M)
SAW_LINE = re.compile(r"^\s*(?:\*\*)?PC5 saw:(?:\*\*)?\s*(.+?)\s*$", re.I | re.M)
GAMES_BY_ISSUE = Path(__file__).with_name("games-by-issue.txt")


def games_by_issue() -> dict:
    """games-by-issue.txt: the games of issues written by hand, by the issue's number."""
    games = {}
    for line in GAMES_BY_ISSUE.read_text(encoding="utf-8").splitlines() if GAMES_BY_ISSUE.exists() else []:
        found = re.match(r"\s*(\d+)\s+(\S.*?)\s*$", line)   # "# …" lines don't match: they say what it is
        if found:
            games.setdefault(int(found.group(1)), []).append(found.group(2))
    return games


def issue_games(issue: dict, fields: dict, named: dict, by_number: dict) -> list:
    """The games an issue is about: games-by-issue.txt's for it, its form's "Game" field, the
    "**Game:**" line of an older PC5's Report on GitHub (without the program in brackets), or
    the game a report names it for. An empty list for an issue about no one game."""
    if issue["number"] in by_number:
        return by_number[issue["number"]]
    if fields.get("Game"):
        return [fields["Game"]]
    found = GAME_LINE.search(issue["body"] or "")
    if found:
        return [re.sub(r"\s*\((?:`[^`]*`|[^()]*\.exe)\)\s*$", "", found.group(1), flags=re.I).strip()]
    if issue["number"] in named:
        return [named[issue["number"]]]
    return []


def first_line(text: str) -> str:
    """A field's first line with something on it, without code marks."""
    return next((line.strip(" `") for line in text.splitlines() if line.strip(" `")), "")


def issue_reports(owner: str, name: str, named: dict) -> list:
    """The rows of the games with an issue, by its state: one a game, its newest issue's."""
    rows, after, by_number = {}, None, games_by_issue()
    while True:
        page = graphql("""query($owner: String!, $name: String!, $after: String) {
            repository(owner: $owner, name: $name) {
              issues(first: 100, after: $after, states: [OPEN, CLOSED], orderBy: {field: CREATED_AT, direction: ASC}) {
                pageInfo { hasNextPage endCursor }
                nodes { number title url body state stateReason createdAt author { login }
                        labels(first: 20) { nodes { name } } }
              } } }""", owner=owner, name=name, after=after)["repository"]["issues"]
        for node in page["nodes"]:
            body = node["body"] or ""
            issue = {"number": node["number"], "title": node["title"], "body": body,
                     "labels": [label["name"].casefold() for label in node["labels"]["nodes"]]}
            fields = answers(body)
            if node["stateReason"] == "DUPLICATE":
                continue
            if node["state"] == "OPEN":
                shown = LOOKED_AT
            elif node["stateReason"] == "NOT_PLANNED" or "wontfix" in issue["labels"]:
                shown = UNRESOLVED
            else:
                shown = FIXED
            pc5 = re.match(r"\s*v?([0-9][0-9.]*)", fields.get("PC5 version", "")) or PC5_LINE.search(body)
            saw = first_line(fields.get("What PC5 saw", "")) or first_line(fields.get("What goes wrong", ""))
            if not saw and SAW_LINE.search(body):
                saw = SAW_LINE.search(body).group(1)
            for game in issue_games(issue, fields, named, by_number):
                rows[game.casefold()] = {   # issues come oldest first: a newer one takes the game's row
                    "told": True, "locked": True, "game": game, "shown": shown,
                    "pc5": pc5.group(1).rstrip(".") if pc5 else "", "edition": "", "controller": "", "settings": "",
                    "notes": saw if shown != FIXED else "",
                    "url": node["url"], "number": node["number"],
                    "author": (node["author"] or {}).get("login", "someone"), "date": node["createdAt"][:10],
                }
        if not page["pageInfo"]["hasNextPage"]:
            return list(rows.values())
        after = page["pageInfo"]["endCursor"]


def label(shown: str) -> str:
    """The table's mark for a result, or for one being looked at."""
    return RESULTS.get(shown, shown)


def linked_issue(owner: str, name: str, text: str):
    """The issue (not a pull request) of this repository that text names, as #61, 61 or its
    link: its number, url, state, stateReason and label names; None when there is none."""
    found = re.search(rf"(?:github\.com/{re.escape(owner)}/{re.escape(name)}/issues/|#|^\s*)(\d+)\b", text, re.I)
    if not found:
        return None
    data = graphql("""query($owner: String!, $name: String!, $number: Int!) {
        repository(owner: $owner, name: $name) {
          issueOrPullRequest(number: $number) {
            __typename
            ... on Issue { number url state stateReason labels(first: 20) { nodes { name } } }
          } } }""", fatal=False, owner=owner, name=name, number=int(found.group(1)))
    if data is None:
        return UNKNOWN
    node = data["repository"]["issueOrPullRequest"]
    if not node or node["__typename"] != "Issue":
        return None
    return {"number": node["number"], "url": node["url"], "state": node["state"], "stateReason": node["stateReason"],
            "labels": [label["name"].casefold() for label in node["labels"]["nodes"]]}


def tell_listed(report: dict, owner: str, name: str) -> int:
    """One comment on a report now in the list, with how it shows there: 1 when made."""
    how = label(report["shown"])
    body = "\n".join([
        LISTED,
        f"Thanks for the report! **{report['game']}** is now in the [game compatibility list]"
        f"(https://github.com/{owner}/{name}/wiki/Game-compatibility) as **{how}**.",
        "",
        "If it runs differently with a newer PC5 or other settings, post a new report, so the list stays up to date.",
    ])
    if graphql("""mutation($id: ID!, $body: String!) { addDiscussionComment(input: {discussionId: $id, body: $body}) {
            comment { id } } }""", fatal=False, id=report["id"], body=body) is None:
        return 0
    print(f"compatibility list: #{report['number']} told it is in the list")
    return 1


def needs_issue(discussion: dict, owner: str, name: str, asked: bool) -> None:
    """A report that a game doesn't run, without an issue: said once, and closed. A failure here
    is a warning: the list is still made."""
    if not asked and not discussion["locked"]:
        body = "\n".join([
            MARK,
            "Thanks for the report! A game that doesn't start or isn't playable is listed by its issue, with its "
            "log, so the cause can be found: it could be the game, its copy, a setting or PC5. This report is "
            "closed.",
            "",
            f"Open an [issue](https://github.com/{owner}/{name}/issues/new/choose) and attach the game's zip "
            "from `/data/prospero-win/logs/` (PC5's **Report on GitHub** writes it for you). The game is then "
            "listed as being looked at, then as working perfectly or unresolved, as the issue is closed.",
        ])
        if graphql("""mutation($id: ID!, $body: String!) { addDiscussionComment(input: {discussionId: $id, body: $body}) {
                comment { id } } }""", fatal=False, id=discussion["id"], body=body) is not None:
            print(f"compatibility list: #{discussion['number']} has no issue: asked for one")
    if not discussion["closed"] and graphql(
            """mutation($id: ID!) { closeDiscussion(input: {discussionId: $id, reason: OUTDATED}) { discussion { id } } }""",
            fatal=False, id=discussion["id"]) is not None:
        print(f"compatibility list: #{discussion['number']} has no issue: closed")


def publish(owner: str, name: str, page: str) -> None:
    """The page as the wiki's Game-compatibility.md, pushed when it changed."""
    token = os.environ["GITHUB_TOKEN"]
    with tempfile.TemporaryDirectory() as work:
        wiki = Path(work) / "wiki"
        subprocess.run(["git", "clone", "-q", "--depth", "1",
                        f"https://x-access-token:{token}@github.com/{owner}/{name}.wiki.git", str(wiki)], check=True)
        target = wiki / "Game-compatibility.md"
        if target.is_file() and target.read_text() == page:
            print("compatibility list: the wiki page is up to date")
            return
        target.write_text(page)
        git = ["git", "-C", str(wiki), "-c", "user.name=github-actions[bot]",
               "-c", "user.email=41898282+github-actions[bot]@users.noreply.github.com"]
        subprocess.run(git + ["add", "Game-compatibility.md"], check=True)
        subprocess.run(git + ["commit", "-q", "-m", "Game compatibility list from the reports"], check=True)
        subprocess.run(git + ["push", "-q", "origin", "HEAD"], check=True)
        print("compatibility list: the wiki page is updated")

if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:   # shown on the run's page as an annotation
        import traceback
        print("::error::compatibility list: " + " | ".join(traceback.format_exception(error))[-900:].replace("\n", " "))
        raise
