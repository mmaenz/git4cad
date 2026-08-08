<script lang="ts">
	import { onMount } from 'svelte';
	import { getCommit, relativeTime, type CommitInfo } from '$lib/api';
	import type { PageProps } from './$types';

	let { params }: PageProps = $props();
	let user = $derived(params.user);
	let repo = $derived(params.repo);
	let sha = $derived(params.sha);

	let commit = $state<CommitInfo | null>(null);
	let loading = $state(true);
	let error = $state('');

	async function load() {
		loading = true;
		error = '';
		try {
			commit = await getCommit(user, repo, sha);
		} catch (e: any) {
			error = e.message || 'Failed to load commit';
		} finally {
			loading = false;
		}
	}

	onMount(load);
</script>

<svelte:head>
	<title>{sha.slice(0, 7)} - {user}/{repo} - git4cad</title>
</svelte:head>

{#if loading}
	<div class="loading-state">
		<div class="spinner"></div>
		<span>Loading commit...</span>
	</div>
{:else if error}
	<div class="alert alert-error">{error}</div>
{:else if commit}
	<div class="commit-detail">
		<div class="commit-header">
			<h2 class="commit-message-full">{commit.message}</h2>
		</div>

		<div class="commit-meta-box">
			<div class="meta-row">
				<div class="meta-author">
					<div class="author-avatar" aria-hidden="true">
						{commit.author.charAt(0).toUpperCase()}
					</div>
					<div class="author-info">
						<span class="author-name">{commit.author}</span>
						<span class="author-email text-muted text-sm">&lt;{commit.email}&gt;</span>
					</div>
				</div>
				<div class="meta-right">
					<span class="text-muted text-sm">committed</span>
					<span class="commit-time text-sm" title={new Date(commit.timestamp * 1000).toISOString()}>
						{relativeTime(new Date(commit.timestamp * 1000).toISOString())}
					</span>
				</div>
			</div>

			<div class="sha-row">
				<span class="sha-label text-muted text-sm">commit</span>
				<code class="sha-full">{commit.sha}</code>
			</div>
		</div>

		<div class="back-link-row">
			<a href="/{user}/{repo}/commits" class="btn btn-sm">
				<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
					<line x1="19" y1="12" x2="5" y2="12"/>
					<polyline points="12 19 5 12 12 5"/>
				</svg>
				All commits
			</a>
		</div>
	</div>
{/if}

<style>
	.loading-state {
		display: flex;
		align-items: center;
		gap: 12px;
		padding: 24px 0;
		color: var(--color-text-muted);
	}

	.commit-detail {
		max-width: 860px;
	}

	.commit-header {
		margin-bottom: 16px;
	}

	.commit-message-full {
		font-size: 20px;
		font-weight: 600;
		color: var(--color-text);
		line-height: 1.4;
		word-break: break-word;
	}

	.commit-meta-box {
		background-color: var(--color-surface);
		border: 1px solid var(--color-border);
		border-radius: var(--radius);
		padding: 16px;
		margin-bottom: 20px;
	}

	.meta-row {
		display: flex;
		align-items: center;
		justify-content: space-between;
		gap: 16px;
		margin-bottom: 12px;
		flex-wrap: wrap;
	}

	.meta-author {
		display: flex;
		align-items: center;
		gap: 10px;
	}

	.author-avatar {
		width: 36px;
		height: 36px;
		border-radius: 50%;
		background: linear-gradient(135deg, var(--color-accent), #6e40c9);
		display: flex;
		align-items: center;
		justify-content: center;
		font-size: 14px;
		font-weight: 700;
		color: white;
		flex-shrink: 0;
	}

	.author-info {
		display: flex;
		flex-direction: column;
		gap: 2px;
	}

	.author-name {
		font-weight: 600;
		font-size: 14px;
	}

	.meta-right {
		display: flex;
		align-items: center;
		gap: 6px;
		color: var(--color-text-muted);
	}

	.commit-time {
		color: var(--color-text-muted);
	}

	.sha-row {
		display: flex;
		align-items: center;
		gap: 8px;
		padding-top: 12px;
		border-top: 1px solid var(--color-border);
	}

	.sha-full {
		font-family: var(--font-mono);
		font-size: 12px;
		color: var(--color-text-muted);
		background: none;
		padding: 0;
	}

	.back-link-row {
		margin-top: 4px;
	}
</style>
