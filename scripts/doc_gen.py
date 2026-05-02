import os
import re
import json

def parse_commands(root_dir):
    commands = {}
    
    # regex for:
    # new Command("name", "description", "category", ...)
    # Command name("name", "description", "category", ...)
    # Command("name", "description", "category", ...)
    cmd_pattern = re.compile(r'(?:new\s+)?Command\s*(?:\s+\w+)?\s*\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"', re.IGNORECASE)
    
    for root, _, files in os.walk(root_dir):
        for file in files:
            if file.endswith('.h') or file.endswith('.cpp'):
                path = os.path.join(root, file)
                with open(path, 'r', encoding='utf-8', errors='ignore') as f:
                    content = f.read()
                    for m in cmd_pattern.finditer(content):
                        name, desc, cat = m.groups()
                        if name not in commands:
                            commands[name] = {
                                'name': name,
                                'description': desc,
                                'category': cat,
                                'subcommands': [],
                                'flags': [],
                                'examples': [],
                                'notes': '',
                                'extended_description': ''
                            }
    return commands

def parse_extended_help(help_file, commands):
    if not os.path.exists(help_file):
        return
        
    with open(help_file, 'r', encoding='utf-8') as f:
        content = f.read()
        
    # Find all set("name", [](Command* c) { ... })
    set_pattern = re.compile(r'set\s*\(\s*"([^"]+)"\s*,\s*\[[^\]]*\]\s*\(\s*Command\s*\*\s*c\s*\)\s*\{([\s\S]*?)\}\s*\)\s*;')
    
    for m in set_pattern.finditer(content):
        name, logic = m.groups()
        if name in commands:
            cmd = commands[name]
            
            # Extract extended_description
            ext_desc_m = re.search(r'c->extended_description\s*=\s*"([^"]+)"', logic)
            if ext_desc_m:
                cmd['extended_description'] = ext_desc_m.group(1)
                
            # Extract notes
            notes_m = re.search(r'c->notes\s*=\s*"([^"]+)"', logic)
            if notes_m:
                cmd['notes'] = notes_m.group(1)
                
            # Extract examples
            examples_m = re.search(r'c->examples\s*=\s*\{([^\}]+)\}', logic)
            if examples_m:
                exs = examples_m.group(1).split(',')
                cmd['examples'] = [ex.strip().strip('"') for ex in exs]
                
            # Extract subcommands
            # c->subcommands = { {"syntax", "explanation"}, ... }
            sub_m = re.search(r'c->subcommands\s*=\s*\{([\s\S]*?)\}\s*;', logic)
            if sub_m:
                sub_entries = re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\}', sub_m.group(1))
                cmd['subcommands'] = [{'syntax': s, 'explanation': e} for s, e in sub_entries]
                
            # Extract flags
            flags_m = re.search(r'c->flags\s*=\s*\{([\s\S]*?)\}\s*;', logic)
            if flags_m:
                flag_entries = re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\}', flags_m.group(1))
                cmd['flags'] = [{'syntax': s, 'explanation': e} for s, e in flag_entries]

def generate_markdown(commands):
    # Group by category
    categories = {}
    for cmd in commands.values():
        cat = cmd['category']
        if cat not in categories:
            categories[cat] = []
        categories[cat].append(cmd)
        
    output = "# BPP Command Reference\n\n"
    output += "This document is automatically generated from the bot's source code.\n\n"
    
    # Table of contents
    output += "## Categories\n"
    for cat in sorted(categories.keys()):
        output += f"- [{cat.capitalize()}](#{cat.lower()})\n"
    output += "\n---\n\n"
    
    for cat in sorted(categories.keys()):
        output += f"## {cat.capitalize()}\n\n"
        for cmd in sorted(categories[cat], key=lambda x: x['name']):
            output += f"### `{cmd['name']}`\n"
            output += f"{cmd['description']}\n\n"
            
            if cmd['extended_description']:
                output += f"{cmd['extended_description']}\n\n"
                
            if cmd['subcommands']:
                output += "**Subcommands:**\n\n"
                for sc in cmd['subcommands']:
                    output += f"- `{cmd['name']} {sc['syntax']}`: {sc['explanation']}\n"
                output += "\n"
                
            if cmd['flags']:
                output += "**Flags:**\n\n"
                for fl in cmd['flags']:
                    output += f"- `{fl['syntax']}`: {fl['explanation']}\n"
                output += "\n"
                
            if cmd['examples']:
                output += "**Examples:**\n"
                for ex in cmd['examples']:
                    output += f"- `{ex}`\n"
                output += "\n"
                
            if cmd['notes']:
                output += f"**Note:** {cmd['notes']}\n\n"
                
            output += "---\n\n"
            
    return output

if __name__ == "__main__":
    commands_dir = "commands"
    help_data_file = "commands/help_data.h"
    
    cmds = parse_commands(commands_dir)
    parse_extended_help(help_data_file, cmds)
    
    md = generate_markdown(cmds)
    
    with open("docs/COMMANDS_REFERENCE.md", "w", encoding="utf-8") as f:
        f.write(md)
        
    print(f"Generated documentation for {len(cmds)} commands.")
