import argparse
import copy
import sys
import re
from typing import Tuple

"""
Changelog generation script, requires PAT with public_repo access, 
see https://github.com/settings/tokens

usage: changelog [-h] [-e END] [-m {full,incremental}] -p PAT [-r REPO] [-s START] [-t TAG]

Generate Changelogs between tags or commits

optional arguments:
  -h, --help            show this help message and exit
  -e END, --end END     Ending reference for Changelog(newest)
  -m {full,incremental}, --mode {full,incremental}
                        Mode to run changelog for [full, incremental]
  -p PAT, --pat PAT     Personal Access Token
  -r REPO, --repo REPO  <org/repo> to generate logs for
  -s START, --start START
                        Starting reference for Changelog(oldest)
  -t TAG, --tag TAG     Tag to use for changelog generation
"""


full = re.compile(r"^(V(\d)+.(\d)+.?(\d)?)$")
incremental = re.compile(r"^(V(\d)+.(\d)+.?(\d)?(RC(\d)+)?(DB(\d)+)?)$")

try:
    from github import Github, UnknownObjectException
    from mdutils import MdUtils
except BaseException:
    sys.exit("Error: run 'pip install PyGithub mdutils'")

SECTIONS = {
    "Major Changes": [
        "major",
    ],
    "Protocol Changes": [
        "protocol change",
    ],
    "Node Configuration Updates": [
        "toml",
        "configuration default change",
    ],
    "RPC Updates": [
        "rpc",
    ],
    "IPC Updates": [
        "ipc",
    ],
    "Websocket Updates": [
        "websockets",
    ],
    "CLI Updates": [
        "cli",
    ],
    "Deprecation/Removal": [
        "deprecation",
        "removal",
    ],
    "Developer Wallet": [
        "qt wallet",
    ],
    "Ledger & Database": [
        "database",
        "database structure",
    ],
    "Developer/Debug Options": [
        "debug",
        "logging",
    ],
    "Fixed Bugs": [
        "bug",
    ],
    "Implemented Enhancements": [
        "enhancement",
        "functionality quality improvements",
        "performance",
        "quality improvements",
    ],
    "Build, Test, Automation, Cleanup & Chores": [
        "build-error",
        "documentation",
        "non-functional change",
        "routine",
        "sanitizers",
        "static-analysis",
        "tool",
        "unit test",
        "universe",
    ],
    "Other": []
}


class cliArgs():
    def __init__(self) -> dict:

        changelog_choices = ["full", "incremental"]

        parse = argparse.ArgumentParser(
            prog="changelog",
            description="Generate Changelogs between tags or commits"
        )

        parse.add_argument(
            '-e', '--end',
            help="Ending reference for Changelog(newest)",
            type=str, action="store",
        )
        parse.add_argument(
            "-m", "--mode",
            help="Mode to run changelog for [full, incremental]",
            type=str, action="store",
            default="incremental",
            choices=changelog_choices
        )
        parse.add_argument(
            '-p', '--pat',
            help="Personal Access Token",
            type=str, action="store",
            required=True,
        )
        parse.add_argument(
            '-r', '--repo',
            help="<org/repo> to generate logs for",
            type=str, action="store",
            default='nanocurrency/nano-node',
        )
        parse.add_argument(
            '-s', '--start',
            help="Starting reference for Changelog(oldest)",
            type=str, action="store",
        )
        parse.add_argument(
            '-t', '--tag',
            help="Tag to use for changelog generation",
            type=str, action="store"
        )
        options = parse.parse_args()
        self.end = options.end
        self.mode = options.mode
        self.pat = options.pat
        self.repo = options.repo.rstrip("/")
        self.start = options.start
        self.tag = options.tag


class generateTree:
    def __init__(self, args):
        github = Github(args.pat)
        self.name = args.repo
        self.repo = github.get_repo(self.name)
        if args.tag:
            self.tag = args.tag
            self.end = self.repo.get_commit(args.tag).sha
        elif args.end:
            self.end = self.repo.get_commit(args.end).sha
        else:
            print("need end or tag")
            exit(1)
        if args.start:
            self.start = self.repo.get_commit(args.start).sha
        else:
            self.start = self.get_common(args.mode)
        self.commits = {}
        self.other_commits = []
        commits = self.repo.get_commits(sha=self.end)
        for commit in commits:
            if commit.sha == self.start:
                break
            m = commit.commit.message.partition('\n')[0]
            try:
                pr_number = int(m[m.rfind('#')+1:m.rfind(')')])
                pull = self.repo.get_pull(pr_number)
            except (ValueError, UnknownObjectException):
                p = commit.get_pulls()
                if p.totalCount > 0:
                    pr_number = p[0].number
                    pull = self.repo.get_pull(pr_number)
                else:
                    print(
                        f"Commit has no associated PR {commit.sha}: \"{m}\"")
                    self.other_commits.append((commit.sha, m))
                    continue

            labels = []
            for label in pull.labels:
                labels.append(label.name)
            self.commits[pull.number] = {
                "Title": pull.title,
                "Url": pull.html_url,
                "labels": labels
            }

    def get_common(self, mode) -> str:
        tags = []
        for tag in self.repo.get_tags():
            if mode == "full":
                found = full.match(tag.name)
            else:
                found = incremental.match(tag.name)
            if found:
                tags.append(tag)
        tree = self.repo.compare(self.end, tags[1].commit.sha)
        return tree.merge_base_commit.sha


class generateMarkdown():
    def __init__(self, repo: generateTree):
        self.mdFile = MdUtils(
            file_name='CHANGELOG', title='CHANGELOG'
        )
        if repo.tag:
            self.mdFile.new_line(
                "## Release " +
                f"[{repo.tag}](https://github.com/{repo.name}/tree/{repo.tag})", wrap_width=0)
        else:
            self.mdFile.new_line(
                f"[{repo.end}](https://github.com/{repo.name}/tree/{repo.end})", wrap_width=0)
        self.mdFile.new_line(f"[Full Changelog](https://github.com/{repo.name}"
                             f"/compare/{repo.start}...{repo.end})", wrap_width=0)
        sort = self.pull_to_section(repo.commits)
        for section, prs in sort.items():
            self.write_header_PR(section)
            for pr in prs:
                self.write_PR(pr, repo.commits[pr[0]])
        if repo.other_commits:
            self.write_header_no_PR()
            for sha, message in repo.other_commits:
                self.write_no_PR(repo, sha, message)
        self.mdFile.create_md_file()

    def write_header_PR(self, section):
        self.mdFile.new_line("---")
        self.mdFile.new_header(level=3, title=section,
                               add_table_of_contents='n')
        self.mdFile.new_line(
            "|Pull Request|Title")
        self.mdFile.new_line("|:-:|:--")

    def write_header_no_PR(self):
        self.mdFile.new_line()
        self.mdFile.new_line(
            "|Commit|Title")
        self.mdFile.new_line("|:-:|:--")

    def write_PR(self, pr, info):
        imp = ""
        if pr[1]:
            imp = "**BREAKING** "
        self.mdFile.new_line(
            f"|[#{pr[0]}]({info['Url']})|{imp}{info['Title']}", wrap_width=0)

    def write_no_PR(self, repo, sha, message):
        url = f"https://github.com/{repo.name}/commit/{sha}"
        self.mdFile.new_line(
            f"|[{sha[:8]}]({url})|{message}", wrap_width=0)

    def handle_labels(self, labels) -> Tuple[str, bool]:
        for section, values in SECTIONS.items():
            for label in labels:
                if label in values:
                    if any(
                            string in labels for string in [
                                'breaking',
                            ]):
                        return section, True
                    else:
                        return section, False
        return 'Other', False

    def pull_to_section(self, commits) -> dict:
        sect = copy.deepcopy(SECTIONS)
        result = {}
        for a in sect:
            sect[a] = []
        for pull, info in commits.items():
            section, important = self.handle_labels(info['labels'])
            if important:
                sect[section].insert(0, [pull, important])
            else:
                sect[section].append([pull, important])
        for a in sect:
            if len(sect[a]) > 0:
                result[a] = sect[a]
        return result


arg = cliArgs()
trees = generateTree(arg)
generateMarkdown(trees)
