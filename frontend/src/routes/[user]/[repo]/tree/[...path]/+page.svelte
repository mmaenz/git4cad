<script lang="ts">
	import { onMount } from 'svelte';
	import { getTree, type TreeEntry } from '$lib/api';
	import FileTree from '$lib/components/FileTree.svelte';
	import CodeBrowserLayout from '$lib/components/CodeBrowserLayout.svelte';
	import type { PageProps } from './$types';

	let { params }: PageProps = $props();
	let user = $derived(params.user);
	let repo = $derived(params.repo);

	// The [...path] param is "ref/optional/sub/path"
	// We split it: first segment = ref, rest = currentPath
	let rawPath = $derived(params.path);

	let ref = $derived(rawPath.split('/')[0] ?? 'main');
	let currentPath = $derived(rawPath.split('/').slice(1).join('/'));

	let entries = $state<TreeEntry[]>([]);
	let loading = $state(true);
	let error = $state('');

	async function load() {
		loading = true;
		error = '';
		try {
			entries = await getTree(user, repo, ref, currentPath || undefined);
		} catch (e: any) {
			error = e.message || 'Failed to load directory';
		} finally {
			loading = false;
		}
	}

	onMount(load);

	// Reload if params change
	$effect(() => {
		// Access reactive deps
		const _u = user, _r = repo, _ref = ref, _path = currentPath;
		load();
	});

	// Breadcrumb parts
	let breadcrumbParts = $derived((() => {
		const parts: { label: string; href: string }[] = [
			{ label: repo, href: `/${user}/${repo}` }
		];
		if (!currentPath) return parts;
		const segments = currentPath.split('/').filter(Boolean);
		segments.forEach((seg, i) => {
			const path = segments.slice(0, i + 1).join('/');
			parts.push({ label: seg, href: `/${user}/${repo}/tree/${ref}/${path}` });
		});
		return parts;
	})());
</script>

<svelte:head>
	<title>{currentPath || repo} - {user}/{repo} - git4cad</title>
</svelte:head>

<div class="tree-page">
	<CodeBrowserLayout {user} {repo} {ref} selectedPath={currentPath}>
		<div class="breadcrumb mb-3">
			{#each breadcrumbParts as part, i (part.href)}
				{#if i > 0}<span class="breadcrumb-sep">/</span>{/if}
				{#if i === breadcrumbParts.length - 1}
					<span class="bc-current">{part.label}</span>
				{:else}
					<a href={part.href} class="bc-link">{part.label}</a>
				{/if}
			{/each}
		</div>

		{#if loading}
			<div class="loading-state">
				<div class="spinner"></div>
				<span>Loading...</span>
			</div>
		{:else if error}
			<div class="alert alert-error">{error}</div>
		{:else}
			<FileTree {entries} {user} {repo} {ref} {currentPath} />
		{/if}
	</CodeBrowserLayout>
</div>

<style>
	.tree-page {
		/* inherits container padding from layout */
	}

	.bc-link {
		color: var(--color-accent);
		text-decoration: none;
		font-weight: 500;
	}

	.bc-link:hover {
		text-decoration: underline;
	}

	.bc-current {
		color: var(--color-text);
		font-weight: 600;
	}

	.loading-state {
		display: flex;
		align-items: center;
		gap: 12px;
		padding: 24px 0;
		color: var(--color-text-muted);
	}
</style>
