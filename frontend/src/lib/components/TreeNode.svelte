<script lang="ts">
	import { getTree, type TreeEntry } from '$lib/api';
	import TreeNode from './TreeNode.svelte';

	interface Props {
		user: string;
		repo: string;
		ref: string;
		entry: TreeEntry;
		parentPath: string;
		selectedPath: string;
		depth: number;
	}

	let { user, repo, ref, entry, parentPath, selectedPath, depth }: Props = $props();

	let fullPath = $derived(parentPath ? `${parentPath}/${entry.name}` : entry.name);
	let isDir = $derived(entry.type === 'tree');
	let isSelected = $derived(fullPath === selectedPath);
	let isAncestorOfSelected = $derived(
		isDir && selectedPath !== '' && selectedPath.startsWith(`${fullPath}/`)
	);

	let expanded = $state(false);
	let children = $state<TreeEntry[] | null>(null);
	let loading = $state(false);
	let loadError = $state(false);

	async function loadChildren() {
		if (children !== null || loading) return;
		loading = true;
		loadError = false;
		try {
			children = await getTree(user, repo, ref, fullPath);
		} catch {
			loadError = true;
		} finally {
			loading = false;
		}
	}

	function toggle() {
		expanded = !expanded;
		if (expanded) loadChildren();
	}

	// Auto-expand + auto-load ancestors of the currently selected path
	$effect(() => {
		if (isAncestorOfSelected && !expanded) {
			expanded = true;
			loadChildren();
		}
	});

	let sortedChildren = $derived(
		children
			? [...children].sort((a, b) => {
					if (a.type !== b.type) return a.type === 'tree' ? -1 : 1;
					return a.name.localeCompare(b.name);
				})
			: []
	);

	let href = $derived(
		isDir ? `/${user}/${repo}/tree/${ref}/${fullPath}` : `/${user}/${repo}/blob/${ref}/${fullPath}`
	);

	let indent = $derived(depth * 16);
</script>

<div class="tree-node">
	<div class="node-row" class:selected={isSelected}>
		{#if isDir}
			<button
				class="chevron-btn"
				style={`margin-left: ${indent}px`}
				onclick={toggle}
				aria-label={expanded ? `Collapse ${entry.name}` : `Expand ${entry.name}`}
				aria-expanded={expanded}
			>
				<svg
					class="chevron"
					class:expanded
					width="12"
					height="12"
					viewBox="0 0 24 24"
					fill="none"
					stroke="currentColor"
					stroke-width="3"
					stroke-linecap="round"
					stroke-linejoin="round"
					aria-hidden="true"
				>
					<polyline points="9 18 15 12 9 6" />
				</svg>
			</button>
		{:else}
			<span class="chevron-spacer" style={`margin-left: ${indent}px`} aria-hidden="true"></span>
		{/if}

		<a {href} class="node-link" title={entry.name}>
			{#if isDir}
				<span class="icon icon-folder" aria-hidden="true">
					<svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true">
						<path d="M10 4H4c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z" />
					</svg>
				</span>
			{:else}
				<span class="icon icon-file" aria-hidden="true">
					<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
						<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z" />
						<polyline points="14 2 14 8 20 8" />
					</svg>
				</span>
			{/if}
			<span class="node-name">{entry.name}</span>
		</a>
	</div>

	{#if isDir && expanded}
		{#if loading && children === null}
			<div class="node-status" style={`padding-left: ${indent + 28}px`}>Loading…</div>
		{:else if loadError}
			<div class="node-status node-status-error" style={`padding-left: ${indent + 28}px`}>Failed to load</div>
		{:else}
			{#each sortedChildren as child (child.sha + child.name)}
				<TreeNode {user} {repo} {ref} entry={child} parentPath={fullPath} {selectedPath} depth={depth + 1} />
			{/each}
		{/if}
	{/if}
</div>

<style>
	.node-row {
		display: flex;
		align-items: center;
		gap: 2px;
		border-radius: var(--radius-sm);
	}

	.node-row:hover {
		background-color: var(--color-surface-hover);
	}

	.node-row.selected {
		background-color: rgba(47, 129, 247, 0.15);
	}

	.chevron-btn {
		display: flex;
		align-items: center;
		justify-content: center;
		width: 20px;
		height: 26px;
		padding: 0;
		background: none;
		border: none;
		border-radius: var(--radius-sm);
		color: var(--color-text-muted);
		cursor: pointer;
		flex-shrink: 0;
	}

	.chevron-btn:hover {
		background-color: var(--color-surface-hover);
		border-color: transparent;
		color: var(--color-text);
	}

	.chevron-spacer {
		display: inline-block;
		width: 20px;
		height: 26px;
		flex-shrink: 0;
	}

	.chevron {
		transition: transform 0.1s ease;
	}

	.chevron.expanded {
		transform: rotate(90deg);
	}

	.node-link {
		display: flex;
		align-items: center;
		gap: 6px;
		min-width: 0;
		flex: 1;
		padding: 5px 8px 5px 2px;
		color: var(--color-text);
		text-decoration: none;
		font-size: 13px;
	}

	.node-link:hover {
		text-decoration: none;
		color: var(--color-text);
	}

	.selected .node-link {
		color: var(--color-accent);
		font-weight: 600;
	}

	.icon {
		flex-shrink: 0;
		display: flex;
		align-items: center;
	}

	.icon-folder {
		color: #54aeff;
	}

	.icon-file {
		color: var(--color-text-muted);
	}

	.node-name {
		overflow: hidden;
		text-overflow: ellipsis;
		white-space: nowrap;
	}

	.node-status {
		padding: 4px 8px;
		font-size: 12px;
		color: var(--color-text-muted);
	}

	.node-status-error {
		color: var(--color-danger);
	}
</style>
