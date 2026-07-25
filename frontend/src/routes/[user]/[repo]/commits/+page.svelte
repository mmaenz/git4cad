<script lang="ts">
	import { page } from '$app/stores';
	import { onMount } from 'svelte';
	import { listCommits, getRepo, type CommitInfo, type RepoInfo } from '$lib/api';
	import CommitList from '$lib/components/CommitList.svelte';

	let user = $derived($page.params.user);
	let repo = $derived($page.params.repo);

	let commits = $state<CommitInfo[]>([]);
	let repoInfo = $state<RepoInfo | null>(null);
	let loading = $state(true);
	let loadingMore = $state(false);
	let error = $state('');
	let hasMore = $state(true);

	const PAGE_SIZE = 30;
	let offset = $state(0);

	async function load() {
		loading = true;
		error = '';
		offset = 0;
		commits = [];
		hasMore = true;
		try {
			repoInfo = await getRepo(user, repo);
			if (!repoInfo.empty) {
				const result = await listCommits(user, repo, repoInfo.default_branch, PAGE_SIZE, 0);
				commits = result;
				offset = result.length;
				hasMore = result.length === PAGE_SIZE;
			}
		} catch (e: any) {
			error = e.message || 'Failed to load commits';
		} finally {
			loading = false;
		}
	}

	async function loadMore() {
		if (loadingMore || !repoInfo) return;
		loadingMore = true;
		try {
			const result = await listCommits(user, repo, repoInfo.default_branch, PAGE_SIZE, offset);
			commits = [...commits, ...result];
			offset += result.length;
			hasMore = result.length === PAGE_SIZE;
		} catch (e: any) {
			error = e.message || 'Failed to load more commits';
		} finally {
			loadingMore = false;
		}
	}

	onMount(load);
</script>

<svelte:head>
	<title>Commits - {user}/{repo} - git4cad</title>
</svelte:head>

{#if loading}
	<div class="loading-state">
		<div class="spinner"></div>
		<span>Loading commits...</span>
	</div>
{:else if error}
	<div class="alert alert-error">{error}</div>
{:else if repoInfo?.empty}
	<div class="empty-state">
		<p>No commits yet.</p>
	</div>
{:else}
	<div class="commits-header">
		<h2 class="commits-title">
			<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
				<circle cx="12" cy="12" r="3"/>
				<line x1="3" y1="12" x2="9" y2="12"/>
				<line x1="15" y1="12" x2="21" y2="12"/>
			</svg>
			Commits on <span class="branch-name">{repoInfo?.default_branch}</span>
		</h2>
	</div>

	<CommitList {commits} {user} {repo} />

	{#if hasMore}
		<div class="load-more">
			<button
				class="btn"
				onclick={loadMore}
				disabled={loadingMore}
			>
				{#if loadingMore}
					<span class="spinner spinner-sm"></span>
					Loading...
				{:else}
					Load more commits
				{/if}
			</button>
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

	.empty-state {
		padding: 32px;
		text-align: center;
		color: var(--color-text-muted);
	}

	.commits-header {
		margin-bottom: 16px;
	}

	.commits-title {
		display: flex;
		align-items: center;
		gap: 8px;
		font-size: 16px;
		font-weight: 400;
		color: var(--color-text-muted);
	}

	.commits-title svg {
		flex-shrink: 0;
	}

	.branch-name {
		font-weight: 600;
		color: var(--color-text);
		font-family: var(--font-mono);
		font-size: 14px;
		background-color: rgba(47, 129, 247, 0.1);
		border: 1px solid rgba(47, 129, 247, 0.2);
		padding: 2px 8px;
		border-radius: 20px;
	}

	.load-more {
		display: flex;
		justify-content: center;
		margin-top: 16px;
	}

	.spinner-sm {
		width: 14px;
		height: 14px;
		border-width: 2px;
	}
</style>
