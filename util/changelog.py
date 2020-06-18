import argparse
import copy

"""
Changelog generation script, requires PAT see https://github.com/settings/tokens
Caveats V20 and prior release tags are tips on their respective release branches
If you try to use a start tag with one of these a full changelog will be generated
since the commit wont appear in your iterations

"""

try:
    from github import Github
    from mdutils import MdUtils
except BaseException:
    exit("Error: run 'pip install PyGithub mdutils'")

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
        "non-functional change",
        "performance",
        "quality improvements",
    ],
    "Build, Test, Automation, & Chores": [
        "build-error",
        "documentation",
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
    def __init__(self):
        parse = argparse.ArgumentParser(
            prog="changelog",
            description="Generate Changelogs between tags or commits"
        )
        parse.add_argument(
            '-r', '--repo',
            help="<org/repo> to generate logs for",
            type=str, action="store",
            default='nanocurrency/nano-node',
        )
        parse.add_argument(
            '-s', '--start',
            help="Starting reference for Changelog",
            type=str, action="store",
            required=True,
        )
        parse.add_argument(
            '-e', '--end',
            help="Ending reference for Changelog",
            type=str, action="store",
            required=True,
        )
        parse.add_argument(
            '--pat',
            help="Personal Access Token",
            type=str, action="store",
            required=True,
        )
        options = parse.parse_args()
        self.repo = options.repo
        self.start = options.start
        self.end = options.end
        self.pat = options.pat

    def __repr__(self):
        return "<cliArgs(repo='{0}', start='{1}', end='{2}', pat='{3}')>" \
            .format(self.repo, self.start, self.end, self.pat)

    def __str__(self):
        return "Generating a changelog for {0} starting with {1} " \
            "and ending with {2}".format(self.repo, self.start, self.end)


class generateTree:
    def __init__(self, args):
        github = Github(args.pat)
        self.repo = github.get_repo(args.repo)
        self.start = args.start
        self.end = args.end
        try:
            self.startCommit = self.repo.get_commit(args.start)
        except BaseException:
            exit("Error finding commit for " + args.start)
        try:
            self.endCommit = self.repo.get_commit(args.end)
        except BaseException:
            exit("Error finding commit for " + args.end)
        commits = self.repo.get_commits(sha=self.endCommit.sha)
        self.commits = {}
        for commit in commits:
            if commit.sha == self.startCommit.sha:
                break
            else:
                for pull in commit.get_pulls():
                    labels = []
                    for label in pull.labels:
                        labels.append(label.name)
                    self.commits[pull.number] = {
                        "Title": pull.title,
                        "Url": pull.html_url,
                        "labels": labels
                    }

    def __repr__(self):
        return "<generateTree(repo='{0}', start='{1}', startCommit='{2}', " \
            "end='{3}', endCommit='{4}', tree='{5}', commits='{6}".format(
                self.repo, self.start, self.startCommit, self.end,
                self.endCommit, self.tree, self.commits,
            )


class generateMarkdown():
    def __repr__(self):
        return "<generateMarkdown(mdFile={0})>".format(
            self.mdFile
        )

    def __init__(self, repo):
        self.mdFile = MdUtils(
            file_name='CHANGELOG', title='CHANGELOG'
        )
        self.mdFile.new_line(
            "## **Release** " + \
            "[{0}](https://github.com/nanocurrency/nano-node/tree/{0})"\
                .format(repo.end))
        self.mdFile.new_line("[Full Changelog](https://github.com/nanocurrency"\
            "/nano-node/compare/{0}...{1})".format(repo.start, repo.end))
        sort = self.pull_to_section(repo.commits)

        for section, prs in sort.items():
            self.write_header(section)
            for pr in prs:
                self.write_PR(pr, repo.commits[pr[0]])
        self.mdFile.create_md_file()

    def write_header(self, section):
        self.mdFile.new_line("---")
        self.mdFile.new_header(level=3, title=section)
        self.mdFile.new_line(
            "|Pull Request|Title")
        self.mdFile.new_line("|:-:|:--")

    def write_PR(self, pr, info):
        imp = ""
        if pr[1]:
            imp = "**BREAKING** "
        self.mdFile.new_line(
            "|[#{0}]({1})|{2}{3}".format(
                pr[0], info['Url'], imp, info['Title']))

    def handle_labels(self, labels):
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

    def pull_to_section(self, commits):
        sect = copy.deepcopy(SECTIONS)
        result = {}
        for a in sect:
            sect[a] = []
        for pull, info in commits.items():
            section, important = self.handle_labels(info['labels'])
            if important:
                sect[section].insert(0,[pull, important])
            else:
                sect[section].append([pull, important])
        for a in sect:
            if len(sect[a]) > 0:
                result[a] = sect[a]
        return result


if __name__ == "__main__":
    args = cliArgs()
    repo = generateTree(args)
    generateMarkdown(repo)
