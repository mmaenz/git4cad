<script lang="ts">
	import { getTree, type TreeEntry } from '$lib/api';
	import TreeNode from './TreeNode.svelte';

	interface Props {
		user: string;
		repo: string;
		ref: string;
		// Path (relative to repo root, no leading slash) of the file/dir currently
		// being viewed. Empty string means the repo root itself.
		selectedPath?: string;
	}

	let { user, repo, ref, selectedPath = '' }: Props = $props();

	let rootEntries = $state<TreeEntry[] | null>(null);
	let loading = $state(true);
	let error = $state('');

	async function load() {
		loading = true;
		error = '';
		try {
			rootEntries = await getTree(user, repo, ref);
		} catch (e: any) {
			error = e.message || 'Failed to load file tree';
		} finally {
			loading = false;
		}
	}

	$effect(() => {
		// Re-fetch whenever the repo/ref changes
		const _u = user, _r = repo, _ref = ref;
		load();
	});

	let sortedRoot = $derived(
		rootEntries
			? [...rootEntries].sort((a, b) => {
					if (a.type !== b.type) return a.type === 'tree' ? -1 : 1;
					return a.name.localeCompare(b.name);
				})
			: []
	);
</script>

<nav class="sidebar" aria-label="Repository file tree">
	<div class="sidebar-body">
		{#if loading}
			<div class="sidebar-status">
				<div class="spinner"></div>
			</div>
		{:else if error}
			<div class="sidebar-status sidebar-error">{error}</div>
		{:else}
			<a href={`/${user}/${repo}`} class="root-row" class:selected={selectedPath === ''}>
				<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
					<path d="M3 3h18v18H3z" />
					<path d="M9 3v18" />
				</svg>
				<span class="root-name">{repo}</span>
			</a>

			{#if sortedRoot.length === 0}
				<div class="sidebar-status">Empty</div>
			{:else}
				{#each sortedRoot as entry (entry.sha + entry.name)}
					<TreeNode {user} {repo} {ref} {entry} parentPath="" {selectedPath} depth={1} />
				{/each}
			{/if}
		{/if}
	</div>
</nav>

<style>
	.sidebar {
		width: 260px;
		flex-shrink: 0;
		align-self: flex-start;
		position: sticky;
		top: 64px;
		max-height: calc(100vh - 80px);
		overflow-y: auto;
		background-color: var(--color-surface);
		border: 1px solid var(--color-border);
		border-radius: var(--radius);
	}

	.sidebar-body {
		padding: 6px;
	}

	.sidebar-status {
		display: flex;
		align-items: center;
		justify-content: center;
		padding: 20px 8px;
		color: var(--color-text-muted);
		font-size: 12px;
	}

	.sidebar-error {
		color: var(--color-danger);
		text-align: center;
	}

	.root-row {
		display: flex;
		align-items: center;
		gap: 8px;
		padding: 6px 8px;
		margin-bottom: 4px;
		border-radius: var(--radius-sm);
		color: var(--color-text);
		text-decoration: none;
		font-size: 13px;
		font-weight: 600;
		border-bottom: 1px solid var(--color-border);
	}

	.root-row:hover {
		text-decoration: none;
		background-color: var(--color-surface-hover);
	}

	.root-row.selected {
		color: var(--color-accent);
	}

	.root-row svg {
		flex-shrink: 0;
		color: var(--color-text-muted);
	}

	.root-name {
		overflow: hidden;
		text-overflow: ellipsis;
		white-space: nowrap;
	}
</style>
