<script lang="ts">
	import { page } from '$app/stores';
	import { onMount } from 'svelte';
	import { getRepo, getTree, getBlobUrl, type RepoInfo, type TreeEntry } from '$lib/api';
	import FileTree from '$lib/components/FileTree.svelte';

	let user = $derived($page.params.user);
	let repo = $derived($page.params.repo);

	let repoInfo = $state<RepoInfo | null>(null);
	let entries = $state<TreeEntry[]>([]);
	let readmeContent = $state<string | null>(null);
	let loading = $state(true);
	let error = $state('');

	async function load() {
		loading = true;
		error = '';
		readmeContent = null;
		try {
			repoInfo = await getRepo(user, repo);
			if (!repoInfo.empty) {
				entries = await getTree(user, repo, repoInfo.default_branch);

				const readme = entries.find(
					(e) => e.type === 'blob' && /^readme(\.md)?$/i.test(e.name)
				);
				if (readme) {
					const url = getBlobUrl(user, repo, repoInfo.default_branch, readme.name);
					const res = await fetch(url);
					if (res.ok) readmeContent = await res.text();
				}
			}
		} catch (e: any) {
			error = e.message || 'Failed to load repository';
		} finally {
			loading = false;
		}
	}

	onMount(load);
</script>

{#if loading}
	<div class="loading-state">
		<div class="spinner"></div>
		<span>Loading...</span>
	</div>
{:else if error}
	<div class="alert alert-error">{error}</div>
{:else if repoInfo?.empty}
	<div class="empty-repo">
		<div class="empty-repo-inner">
			<h3>This repository is empty</h3>
			<p>Get started by pushing an existing repository:</p>
			{#if repoInfo.clone_url}
				<pre class="setup-code">git remote add origin {repoInfo.clone_url}
git branch -M {repoInfo.default_branch}
git push -u origin {repoInfo.default_branch}</pre>
			{/if}
			<p class="setup-hint">Or create a new repository on the command line:</p>
			{#if repoInfo.clone_url}
				<pre class="setup-code">echo "# {repo}" >> README.md
git init
git add README.md
git commit -m "Initial commit"
git remote add origin {repoInfo.clone_url}
git branch -M {repoInfo.default_branch}
git push -u origin {repoInfo.default_branch}</pre>
			{/if}
		</div>
	</div>
{:else}
	<div class="ref-bar">
		<div class="ref-badge">
			<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
				<line x1="6" y1="3" x2="6" y2="15"/>
				<circle cx="18" cy="6" r="3"/>
				<circle cx="6" cy="18" r="3"/>
				<path d="M18 9a9 9 0 0 1-9 9"/>
			</svg>
			{repoInfo?.default_branch}
		</div>
	</div>

	<FileTree
		{entries}
		{user}
		{repo}
		ref={repoInfo?.default_branch ?? 'main'}
		currentPath=""
	/>

	{#if readmeContent !== null}
		<div class="readme-section">
			<div class="readme-header">
				<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
					<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/>
					<polyline points="14 2 14 8 20 8"/>
					<line x1="16" y1="13" x2="8" y2="13"/>
					<line x1="16" y1="17" x2="8" y2="17"/>
					<polyline points="10 9 9 9 8 9"/>
				</svg>
				README
			</div>
			<div class="readme-body">
				<pre class="readme-content">{readmeContent}</pre>
			</div>
		</div>
	{/if}
{/if}

<style>
	.loading-state {
		display: flex;
		align-items: center;
		gap: 12px;
		padding: 24px 0;
		color: var(--color-text-muted);
	}

	.empty-repo {
		display: flex;
		justify-content: center;
		padding: 32px 0;
	}

	.empty-repo-inner {
		max-width: 600px;
		width: 100%;
	}

	.empty-repo h3 {
		font-size: 20px;
		margin-bottom: 16px;
	}

	.empty-repo p {
		color: var(--color-text-muted);
		margin-bottom: 12px;
	}

	.setup-hint {
		margin-top: 20px;
	}

	.setup-code {
		font-family: var(--font-mono);
		font-size: 13px;
		background-color: var(--color-surface);
		border: 1px solid var(--color-border);
		border-radius: var(--radius);
		padding: 16px;
		overflow-x: auto;
		line-height: 1.7;
		white-space: pre;
		margin-bottom: 12px;
	}

	.ref-bar {
		margin-bottom: 12px;
	}

	.ref-badge {
		display: inline-flex;
		align-items: center;
		gap: 6px;
		padding: 4px 10px;
		background-color: var(--color-surface);
		border: 1px solid var(--color-border);
		border-radius: 20px;
		font-size: 13px;
		font-weight: 500;
		color: var(--color-text);
	}

	.readme-section {
		margin-top: 24px;
		background-color: var(--color-surface);
		border: 1px solid var(--color-border);
		border-radius: var(--radius);
		overflow: hidden;
	}

	.readme-header {
		display: flex;
		align-items: center;
		gap: 8px;
		padding: 10px 16px;
		border-bottom: 1px solid var(--color-border);
		background-color: var(--color-surface-hover);
		font-size: 14px;
		font-weight: 600;
		color: var(--color-text);
	}

	.readme-body {
		padding: 24px;
	}

	.readme-content {
		background: none;
		border: none;
		padding: 0;
		font-family: var(--font-sans);
		font-size: 14px;
		line-height: 1.7;
		white-space: pre-wrap;
		word-break: break-word;
		color: var(--color-text);
	}
</style>
