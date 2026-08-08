<script lang="ts">
	import { onMount } from 'svelte';
	import { listRepos, type RepoInfo } from '$lib/api';
	import RepoCard from '$lib/components/RepoCard.svelte';
	import type { PageProps } from './$types';

	let { params }: PageProps = $props();
	let user = $derived(params.user);

	let repos = $state<RepoInfo[]>([]);
	let loading = $state(true);
	let error = $state('');

	async function load() {
		loading = true;
		error = '';
		try {
			const all = await listRepos();
			repos = all.filter((r) => r.owner === user);
		} catch (e: any) {
			error = e.message || 'Failed to load repositories';
		} finally {
			loading = false;
		}
	}

	onMount(load);
</script>

<svelte:head>
	<title>{user} - git4cad</title>
</svelte:head>

<div class="container page">
	<div class="profile-header">
		<div class="avatar" aria-hidden="true">
			{user.charAt(0).toUpperCase()}
		</div>
		<div class="profile-info">
			<h1 class="profile-username">{user}</h1>
		</div>
	</div>

	<div class="profile-content">
		<h2 class="section-title">Repositories</h2>

		{#if error}
			<div class="alert alert-error">{error}</div>
		{/if}

		{#if loading}
			<div class="loading-state">
				<div class="spinner"></div>
				<span>Loading...</span>
			</div>
		{:else if repos.length === 0}
			<div class="empty-state">
				<p>{user} has no public repositories yet.</p>
			</div>
		{:else}
			<div class="repo-grid">
				{#each repos as repo (repo.owner + '/' + repo.name)}
					<RepoCard {repo} />
				{/each}
			</div>
		{/if}
	</div>
</div>

<style>
	.profile-header {
		display: flex;
		align-items: center;
		gap: 20px;
		margin-bottom: 32px;
		padding-bottom: 24px;
		border-bottom: 1px solid var(--color-border);
	}

	.avatar {
		width: 72px;
		height: 72px;
		border-radius: 50%;
		background: linear-gradient(135deg, var(--color-accent), #6e40c9);
		display: flex;
		align-items: center;
		justify-content: center;
		font-size: 28px;
		font-weight: 700;
		color: white;
		flex-shrink: 0;
	}

	.profile-username {
		font-size: 26px;
		font-weight: 600;
	}

	.profile-content {
		/* side-by-side could go here but keep it simple */
	}

	.section-title {
		font-size: 16px;
		font-weight: 600;
		margin-bottom: 16px;
		padding-bottom: 8px;
		border-bottom: 1px solid var(--color-border);
	}

	.repo-grid {
		display: grid;
		grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
		gap: 12px;
	}

	.loading-state {
		display: flex;
		align-items: center;
		gap: 12px;
		padding: 24px;
		color: var(--color-text-muted);
	}

	.empty-state {
		padding: 32px;
		text-align: center;
		color: var(--color-text-muted);
	}
</style>
