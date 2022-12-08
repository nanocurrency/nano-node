import argparse
import copy
import sys
import re
from typing import Tuple
import subprocess

"""
Changelog generation script, requires PAT with public_repo access, 
see https://github.com/settings/tokens

usage: changelog [-h] [-e END] [-m {final,beta}] -p PAT [-r REPO] [-s START] [-t TAG]

Generate Changelogs between tags or commits

optional arguments:
  -h, --help            Show this help message and exit
  -m {final,beta}, --mode {final,beta}
                        Mode to run changelog for [final, beta]
  -p PAT, --pat PAT     Personal Access Token
  -r REPO, --repo REPO  <org/repo> to generate logs for
  -e END, --end END     Ending reference for Changelog(newest)
  -s START, --start START
                        Starting reference for Changelog(oldest)
  -t TAG, --tag TAG
                        Tag to use for changelog generation
  --start-tag START_TAG 
                        Tag to use as start reference (instead of -s)
  --previous-branch PREVIOUS_BRANCH
                        Branch name of the previous version
                        Reference: --tag or --end
  -v, --verbose         Verbose mode
"""


final = re.compile(r"^(V(\d)+.(\d)+)$")
beta = re.compile(r"^(V(\d)+.(\d)+((RC(\d)+)|(DB(\d)+))?)$")

try:
    from github import Github, UnknownObjectException
    from github.Label import Label
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


class CliArgs:
    def __init__(self) -> dict:

        changelog_choices = ["final", "beta"]

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
            help="Mode to run changelog for [final, beta]",
            type=str, action="store",
            default="beta",
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
        parse.add_argument(
            '--start-tag',
            dest='start_tag',
            help="Tag for start reference",
            type=str, action="store"
        )
        parse.add_argument(
            '--previous-branch',
            dest='previous_branch',
            help='Branch name of the previous version',
            type=str, action='store',
            required=True
        )
        parse.add_argument(
            '-v', '--verbose',
            help="Verbose mode",
            action="store_true"
        )
        options = parse.parse_args()
        self.end = options.end
        self.mode = options.mode
        self.pat = options.pat
        self.repo = options.repo.rstrip("/")
        self.start = options.start
        self.tag = options.tag
        self.verbose = options.verbose
        self.previous_branch = options.previous_branch
        self.start_tag = options.start_tag


def validate_sha(hash_value: str) -> bool:
    if len(hash_value) != 40:
        return False
    try:
        sha_int = int(hash_value, 16)
    except ValueError:
        return False
    return True


class GenerateTree:
    def __init__(self, args):
        github = Github(args.pat)
        self.name = args.repo
        self.repo = github.get_repo(self.name)
        self.args = args
        self.previous_branch = args.previous_branch
        if args.tag:
            self.tag = args.tag
            self.end = self.repo.get_commit(args.tag).sha
            if args.end:
                print("error: set either --end or --tag")
                exit(1)
        if args.end:
            if not validate_sha(args.end):
                print("error: --end argument is not a valid hash")
                exit(1)
            self.end = self.repo.get_commit(args.end).sha
            if not args.start:
                print("error: --end argument requires --start")
                exit(1)
        if not args.end and not args.tag:
            print("error: need either --end or --tag")
            exit(1)
        if args.start and args.start_tag:
            print("error: set either --start or --start-tag")
            exit(1)
        if args.start:
            if not validate_sha(args.start):
                print("error: --start argument is not a valid hash")
                exit(1)
            self.start = self.repo.get_commit(args.start).sha
        elif args.start_tag:
            self.start = self.select_start_ref(args.start_tag)
        else:
            assert args.tag
            self.start = self.get_common_by_tag(args.mode)

        self.commits = {}
        self.other_commits = []
        self.excluded = []
        commits = self.repo.get_commits(sha=self.end)

        # Check if the common ancestor exists in the commit list.
        found_common_ancestor = False
        for commit in commits:
            if commit.sha == self.start:
                found_common_ancestor = True
                break
        if not found_common_ancestor:
            print("error: the common ancestor was not found")
            exit(1)

        # Retrieve the complementary information for each commit.
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
                    if args.verbose:
                        print(f"info: commit has no associated PR {commit.sha}: \"{m}\"")
                    self.other_commits.append((commit.sha, m))
                    continue

            if pull.state == 'open':
                if args.verbose:
                    print(f"info: commit is in tree but only associated with open PR {pr_number}: \"{pull.title}\"")
                self.other_commits.append((commit.sha, m))
                continue

            if self.excluded_from_changelog(pull.labels):
                if args.verbose:
                    print(f"info: the PR {pr_number}: \"{pull.title}\" was excluded from the changelog")
                self.excluded.append((commit.sha, m))
                continue

            labels = []
            for label in pull.labels:
                labels.append(label.name)

            self.commits[pull.number] = {
                "Title": pull.title,
                "Url": pull.html_url,
                "labels": labels
            }

    @staticmethod
    def excluded_from_changelog(labels: list[Label]) -> bool:
        for label in labels:
            if label.name == 'exclude from changelog':
                return True
        return False

    def get_common_ancestor(self) -> str:
        if not self.previous_branch:
            print(f"argument required: the target end or tag doesn't have a {self.args.mode} ancestor in the same"
                  "major version, the argument --previous-branch is required to find the ancestor")
            exit(1)

        print("info: will look for the common ancestor by local git repo")
        cmd = f'''
        repo_path=/tmp/$(uuid)
        (
            mkdir -p "$repo_path"
            if [[ ! -d $repo_path || ! -z "$(ls -A $repo_path)" ]]; then
                exit 1
            fi
            pushd "$repo_path"
            git clone https://github.com/{self.name} .
            git checkout origin/{self.previous_branch} -b {self.previous_branch}
            common_ancestor=$( \
                diff -u <(git rev-list --first-parent "develop") \
                <(git rev-list --first-parent "HEAD") \
                | sed -ne "s/^ //p" | head -1 \
            )
            echo "$common_ancestor" > "$repo_path/output_file"
            popd
        ) > /dev/null 2>&1
        cat "$repo_path/output_file"
        rm -rf "$repo_path"
        '''
        common_ancestor = subprocess.check_output(f"echo '{cmd}' | /bin/bash", shell=True, text=True).rstrip()
        if self.args.verbose:
            print("info: found common ancestor: " + common_ancestor)
        return common_ancestor

    def get_common_by_tag(self, mode) -> str:
        tags = []
        found_end_tag = False
        for tag in self.repo.get_tags():
            if not found_end_tag and tag.name == self.tag:
                found_end_tag = True
            if found_end_tag:
                if mode == "final":
                    matched_tag = final.match(tag.name)
                else:
                    matched_tag = beta.match(tag.name)
                if matched_tag:
                    tags.append(tag)

        if len(tags) < 2:
            return None

        selected_tag = None
        if self.major_version_match(tags[0].name, self.tag):
            selected_tag = tags[1]
        else:
            selected_tag = tags[0]

        if self.args.verbose:
            print(f"info: selected start tag {selected_tag.name}: {selected_tag.commit.sha}")

        return self.select_start_ref(selected_tag.name)

    @staticmethod
    def major_version_match(first_tag: str, second_tag: str) -> bool:
        major_version_tag_pattern = r"(\d)+."
        first_tag_major = re.search(major_version_tag_pattern, first_tag)
        second_tag_major = re.search(major_version_tag_pattern, second_tag)
        if first_tag_major and second_tag_major and first_tag_major.group(0) == second_tag_major.group(0):
            return True
        return False

    def select_start_ref(self, start_tag: str) -> str:
        if self.major_version_match(start_tag, self.tag):
            start_commit = self.repo.get_commit(start_tag).sha
            if self.args.verbose:
                print(f"info: selected start tag {start_tag} (commit: {start_commit}) "
                      f"has the same major version of the end tag ({self.tag})")
            return start_commit

        return self.get_common_ancestor()


class GenerateMarkdown:
    def __init__(self, repo: GenerateTree):
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
            self.write_header_pr(section)
            for pr in prs:
                self.write_pr(pr, repo.commits[pr[0]])
        if repo.other_commits:
            self.write_header_no_pr()
            for sha, message in repo.other_commits:
                self.write_no_pr(repo, sha, message)
        self.mdFile.create_md_file()

    def write_header_pr(self, section):
        self.mdFile.new_line("---")
        self.mdFile.new_header(level=3, title=section,
                               add_table_of_contents='n')
        self.mdFile.new_line(
            "|Pull Request|Title")
        self.mdFile.new_line("|:-:|:--")

    def write_header_no_pr(self):
        self.mdFile.new_line()
        self.mdFile.new_line(
            "|Commit|Title")
        self.mdFile.new_line("|:-:|:--")

    def write_pr(self, pr, info):
        imp = ""
        if pr[1]:
            imp = "**BREAKING** "
        self.mdFile.new_line(
            f"|[#{pr[0]}]({info['Url']})|{imp}{info['Title']}", wrap_width=0)

    def write_no_pr(self, repo, sha, message):
        url = f"https://github.com/{repo.name}/commit/{sha}"
        self.mdFile.new_line(
            f"|[{sha[:8]}]({url})|{message}", wrap_width=0)

    @staticmethod
    def handle_labels(labels) -> Tuple[str, bool]:
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


arg = CliArgs()
trees = GenerateTree(arg)
GenerateMarkdown(trees)
