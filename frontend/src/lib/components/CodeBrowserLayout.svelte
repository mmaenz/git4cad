<script lang="ts">
	import type { Snippet } from 'svelte';
	import FileTreeSidebar from './FileTreeSidebar.svelte';

	interface Props {
		user: string;
		repo: string;
		ref: string;
		selectedPath?: string;
		children: Snippet;
	}

	let { user, repo, ref, selectedPath = '', children }: Props = $props();
</script>

<div class="code-browser">
	<FileTreeSidebar {user} {repo} {ref} {selectedPath} />
	<div class="code-main">
		{@render children()}
	</div>
</div>

<style>
	.code-browser {
		display: flex;
		align-items: flex-start;
		gap: 16px;
	}

	.code-main {
		flex: 1;
		min-width: 0;
	}

	@media (max-width: 800px) {
		.code-browser {
			flex-direction: column;
		}

		.code-browser :global(.sidebar) {
			width: 100%;
			position: static;
			max-height: 300px;
		}
	}
</style>
